using ScreenRecorderLib;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text.Json;

namespace BareRec;

public sealed class MainForm : Form
{
    private const int HotkeyId = 101;
    private const int WmHotkey = 0x0312;
    private const uint Ctrl = 0x0002, Shift = 0x0004;
    private readonly Color bg = Color.FromArgb(17, 21, 31);
    private readonly Color accent = Color.FromArgb(68, 132, 255);
    private readonly TextBox folder = new();
    private readonly CheckBox systemAudio = new(), microphone = new();
    private readonly ComboBox fps = new();
    private readonly Button record = new(), browse = new(), openFolder = new();
    private readonly Label status = new(), elapsed = new();
    private readonly NotifyIcon tray = new();
    private readonly System.Windows.Forms.Timer timer = new() { Interval = 1000 };
    private Recorder? recorder;
    private DateTime started;
    private bool recording, starting, stopping, hotkeyRegistered, closingAfterStop;
    private Settings settings = new();
    private string output = "";

    public MainForm()
    {
        Text = "BareRec";
        ClientSize = new Size(490, 388);
        MinimumSize = MaximumSize = Size;
        FormBorderStyle = FormBorderStyle.FixedSingle;
        MaximizeBox = false;
        StartPosition = FormStartPosition.CenterScreen;
        BackColor = bg;
        ForeColor = Color.White;
        Font = new Font("Segoe UI", 10);
        LoadSettings();

        AddLabel("BareRec", 26, 18, 30, FontStyle.Bold);
        AddLabel("Record. Save. Done.", 26, 66, 11, FontStyle.Regular);
        AddLabel("Save location", 26, 108, 10, FontStyle.Regular);

        folder.SetBounds(26, 138, 315, 30);
        folder.Text = settings.Folder;
        folder.BackColor = Color.FromArgb(33, 39, 53);
        folder.ForeColor = Color.White;
        folder.BorderStyle = BorderStyle.FixedSingle;
        folder.TextChanged += (_, _) => SaveSettings();
        Controls.Add(folder);
        SetupButton(browse, "Browse", 350, 136, 113, 34);
        browse.Click += (_, _) =>
        {
            using var dialog = new FolderBrowserDialog();
            if (Directory.Exists(folder.Text)) dialog.SelectedPath = folder.Text;
            if (dialog.ShowDialog(this) == DialogResult.OK) folder.Text = dialog.SelectedPath;
        };

        systemAudio.Text = "System audio";
        systemAudio.Checked = settings.SystemAudio;
        systemAudio.SetBounds(26, 194, 210, 29);
        systemAudio.CheckedChanged += (_, _) => SaveSettings();
        Controls.Add(systemAudio);

        microphone.Text = "Microphone";
        microphone.Checked = settings.Microphone;
        microphone.SetBounds(245, 194, 180, 29);
        microphone.CheckedChanged += (_, _) => SaveSettings();
        Controls.Add(microphone);

        AddLabel("FPS", 26, 244, 10, FontStyle.Regular);
        fps.SetBounds(72, 240, 90, 32);
        fps.DropDownStyle = ComboBoxStyle.DropDownList;
        fps.Items.AddRange(["30", "60"]);
        fps.SelectedItem = settings.Fps == 60 ? "60" : "30";
        fps.SelectedIndexChanged += (_, _) => SaveSettings();
        Controls.Add(fps);
        AddLabel("Ctrl + Shift + F9: start / stop", 176, 244, 10, FontStyle.Regular);

        SetupButton(record, "●  Start recording", 26, 297, 437, 48);
        record.BackColor = accent;
        record.Font = new Font("Segoe UI", 12, FontStyle.Bold);
        record.Click += async (_, _) => await ToggleAsync();

        SetupButton(openFolder, "Open recordings", 26, 352, 180, 28);
        openFolder.Click += (_, _) => OpenFolder();
        status.SetBounds(216, 353, 247, 28);
        status.TextAlign = ContentAlignment.MiddleRight;
        status.Font = new Font("Segoe UI", 9);
        status.Text = "Ready";
        Controls.Add(status);

        elapsed.SetBounds(350, 30, 113, 32);
        elapsed.TextAlign = ContentAlignment.MiddleRight;
        elapsed.Text = "00:00:00";
        Controls.Add(elapsed);
        timer.Tick += (_, _) => elapsed.Text = (DateTime.Now - started).ToString(@"hh\:mm\:ss");

        var trayMenu = new ContextMenuStrip();
        trayMenu.Items.Add("Show BareRec", null, (_, _) => RestoreWindow());
        trayMenu.Items.Add("Stop recording", null, (_, _) => StopRecording());
        tray.Icon = SystemIcons.Application;
        tray.Text = "BareRec";
        tray.ContextMenuStrip = trayMenu;
        tray.DoubleClick += (_, _) => RestoreWindow();

        Shown += (_, _) =>
        {
            hotkeyRegistered = RegisterHotKey(Handle, HotkeyId, Ctrl | Shift, 0x78); // F9
            if (!hotkeyRegistered) status.Text = "Hotkey unavailable";
        };
        FormClosing += OnClosing;
        FormClosed += (_, _) =>
        {
            if (hotkeyRegistered) UnregisterHotKey(Handle, HotkeyId);
            tray.Dispose();
            timer.Dispose();
            recorder?.Dispose();
        };
    }

    private void AddLabel(string text, int x, int y, int fontSize, FontStyle style)
    {
        var label = new Label
        {
            Text = text, AutoSize = true, Location = new Point(x, y),
            ForeColor = Color.White, Font = new Font("Segoe UI", fontSize, style)
        };
        Controls.Add(label);
    }

    private void SetupButton(Button button, string text, int x, int y, int w, int h)
    {
        button.Text = text;
        button.SetBounds(x, y, w, h);
        button.FlatStyle = FlatStyle.Flat;
        button.FlatAppearance.BorderSize = 0;
        button.BackColor = Color.FromArgb(43, 52, 70);
        button.ForeColor = Color.White;
        button.Cursor = Cursors.Hand;
        Controls.Add(button);
    }

    private async Task ToggleAsync()
    {
        if (starting || stopping) return;
        if (recording) StopRecording();
        else await StartRecordingAsync();
    }

    private async Task StartRecordingAsync()
    {
        starting = true;
        record.Enabled = false;
        try
        {
            string destination = folder.Text.Trim();
            if (string.IsNullOrWhiteSpace(destination)) throw new InvalidOperationException("Browse tallennuskansio.");
            Directory.CreateDirectory(destination);
            output = Path.Combine(destination, "BareRec_" + DateTime.Now.ToString("yyyy-MM-dd_HH-mm-ss-fff") + ".mp4");
            int selectedFps = fps.SelectedItem?.ToString() == "60" ? 60 : 30;

            var options = RecorderOptions.DefaultMainMonitor;
            options.VideoEncoderOptions = new VideoEncoderOptions
            {
                Framerate = selectedFps,
                Bitrate = selectedFps == 60 ? 8_000_000 : 5_000_000,
                Encoder = new H264VideoEncoder
                {
                    BitrateMode = H264BitrateControlMode.CBR,
                    EncoderProfile = H264Profile.Main
                },
                IsFixedFramerate = true,
                IsHardwareEncodingEnabled = true
            };
            options.AudioOptions = new AudioOptions
            {
                IsAudioEnabled = systemAudio.Checked || microphone.Checked,
                Bitrate = AudioBitrate.bitrate_128kbps,
                Channels = AudioChannels.Stereo
            };
            if (systemAudio.Checked) options.AudioOptions.AudioSources.Add(LoopbackAudioSource.Default);
            if (microphone.Checked) options.AudioOptions.AudioSources.Add(CaptureAudioSource.Default);
            options.MouseOptions = new MouseOptions { IsMousePointerEnabled = true };

            recorder = Recorder.CreateRecorder(options);
            recorder.OnRecordingComplete += OnComplete;
            recorder.OnRecordingFailed += OnFailed;

            WindowState = FormWindowState.Minimized;
            await Task.Delay(450);
            recorder.Record(output);
            recording = true;
            stopping = false;
            started = DateTime.Now;
            timer.Start();
            status.Text = "Recording...";
            record.Text = "■  Stop and save";
            record.BackColor = Color.FromArgb(218, 77, 86);
            fps.Enabled = systemAudio.Enabled = microphone.Enabled = false;
            tray.Visible = true;
        }
        catch (Exception ex)
        {
            recorder?.Dispose();
            recorder = null;
            Finish();
            RestoreWindow();
            MessageBox.Show(this, ex.Message, "BareRec — error", MessageBoxButtons.OK, MessageBoxIcon.Error);
        }
        finally
        {
            starting = false;
            if (!stopping) record.Enabled = true;
        }
    }

    private void StopRecording()
    {
        if (!recording || stopping) return;
        stopping = true;
        timer.Stop();
        status.Text = "Finalizing MP4...";
        record.Enabled = false;
        try { recorder?.Stop(); }
        catch (Exception ex) { OnFailureUi(ex.Message); }
    }

    private void OnComplete(object? sender, RecordingCompleteEventArgs e)
    {
        string fileName = Path.GetFileName(e.FilePath);
        Ui(() =>
        {
            Finish();
            status.Text = "Saved: " + fileName;
            if (closingAfterStop) Close();
        });
    }

    private void OnFailed(object? sender, RecordingFailedEventArgs e) => Ui(() => OnFailureUi(e.Error));

    private void OnFailureUi(string error)
    {
        Finish();
        RestoreWindow();
        MessageBox.Show(this, error, "Recording failed", MessageBoxButtons.OK, MessageBoxIcon.Error);
        if (closingAfterStop) Close();
    }

    private void Ui(Action action)
    {
        if (IsDisposed || !IsHandleCreated) return;
        try { BeginInvoke(action); } catch (InvalidOperationException) { }
    }

    private void Finish()
    {
        timer.Stop();
        recording = stopping = false;
        record.Enabled = true;
        record.Text = "●  Start recording";
        record.BackColor = accent;
        fps.Enabled = systemAudio.Enabled = microphone.Enabled = true;
        tray.Visible = false;
        recorder?.Dispose();
        recorder = null;
    }

    private void RestoreWindow()
    {
        Show();
        WindowState = FormWindowState.Normal;
        Activate();
    }

    private void OpenFolder()
    {
        try
        {
            Directory.CreateDirectory(folder.Text);
            Process.Start(new ProcessStartInfo(folder.Text) { UseShellExecute = true });
        }
        catch (Exception ex) { MessageBox.Show(this, ex.Message, "Could not open folder"); }
    }

    private static string SettingsPath => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData),
        "BareRec", "settings.json");

    private void LoadSettings()
    {
        try
        {
            if (File.Exists(SettingsPath))
                settings = JsonSerializer.Deserialize<Settings>(File.ReadAllText(SettingsPath)) ?? new Settings();
        }
        catch { settings = new Settings(); }
        if (string.IsNullOrEmpty(settings.Folder))
            settings.Folder = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyVideos), "BareRec");
    }

    private void SaveSettings()
    {
        try
        {
            if (folder is null || fps is null || systemAudio is null || microphone is null) return;
            settings.Folder = folder.Text;
            settings.Fps = fps.SelectedItem?.ToString() == "60" ? 60 : 30;
            settings.SystemAudio = systemAudio.Checked;
            settings.Microphone = microphone.Checked;
            Directory.CreateDirectory(Path.GetDirectoryName(SettingsPath)!);
            File.WriteAllText(SettingsPath, JsonSerializer.Serialize(settings));
        }
        catch { /* Settings persistence must not interrupt recording. */ }
    }

    private void OnClosing(object? sender, FormClosingEventArgs e)
    {
        if (recording && !closingAfterStop)
        {
            e.Cancel = true;
            closingAfterStop = true;
            StopRecording();
        }
        else if (stopping) e.Cancel = true;
    }

    protected override void WndProc(ref Message m)
    {
        if (m.Msg == WmHotkey && m.WParam.ToInt32() == HotkeyId)
        {
            _ = ToggleAsync();
            return;
        }
        base.WndProc(ref m);
    }

    [DllImport("user32.dll")]
    private static extern bool RegisterHotKey(IntPtr hWnd, int id, uint fsModifiers, uint vk);
    [DllImport("user32.dll")]
    private static extern bool UnregisterHotKey(IntPtr hWnd, int id);

    public sealed class Settings
    {
        public string Folder { get; set; } = "";
        public int Fps { get; set; } = 30;
        public bool SystemAudio { get; set; } = true;
        public bool Microphone { get; set; }
    }
}