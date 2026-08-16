using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace MiSTerCast
{
    internal static class Program
    {
        private sealed class Options
        {
            public string Target;
            public string ModelineName = "320x240 NTSC (60Hz)";
            public string SwitchModelineName;
            public string ModelinesPath = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "modelines.dat");
            public int DurationSeconds = 10;
            public int Display = 1;
            public bool Audio = true;
            public bool TestPattern;
            public ushort? CaptureWidth;
            public ushort? CaptureHeight;
            public bool ListModelines;
            public bool ShowHelp;
        }

        private sealed class Modeline
        {
            public string Name;
            public double PixelClock;
            public ushort HActive;
            public ushort HBegin;
            public ushort HEnd;
            public ushort HTotal;
            public ushort VActive;
            public ushort VBegin;
            public ushort VEnd;
            public ushort VTotal;
            public bool Interlace;
        }

        private sealed class TestPatternHost : IDisposable
        {
            private readonly int displayIndex;
            private readonly ManualResetEventSlim shown = new ManualResetEventSlim(false);
            private Thread thread;
            private TestPatternForm form;

            public TestPatternHost(int display)
            {
                displayIndex = display - 1;
            }

            public void Start()
            {
                thread = new Thread(Run);
                thread.IsBackground = true;
                thread.SetApartmentState(ApartmentState.STA);
                thread.Start();
                if (!shown.Wait(TimeSpan.FromSeconds(5)))
                    throw new InvalidOperationException("The full-screen test pattern did not open.");
            }

            private void Run()
            {
                Application.EnableVisualStyles();
                Application.SetCompatibleTextRenderingDefault(false);
                Screen[] screens = Screen.AllScreens;
                if (displayIndex < 0 || displayIndex >= screens.Length)
                {
                    shown.Set();
                    return;
                }

                form = new TestPatternForm(screens[displayIndex]);
                form.Shown += (sender, args) => shown.Set();
                Application.Run(form);
            }

            public void Dispose()
            {
                if (form != null && form.IsHandleCreated)
                    form.BeginInvoke(new Action(form.Close));
                if (thread != null && !thread.Join(TimeSpan.FromSeconds(5)))
                    QueueLog("Test-pattern window did not close within five seconds.", true);
                shown.Dispose();
            }
        }

        private sealed class TestPatternForm : Form
        {
            [DllImport("winmm.dll")]
            private static extern uint timeBeginPeriod(uint period);

            [DllImport("winmm.dll")]
            private static extern uint timeEndPeriod(uint period);

            private readonly System.Windows.Forms.Timer timer;
            private readonly Stopwatch stopwatch = Stopwatch.StartNew();
            private readonly Font counterFont = new Font(FontFamily.GenericMonospace, 72, FontStyle.Bold, GraphicsUnit.Pixel);
            private readonly StringFormat centered = new StringFormat
            {
                Alignment = StringAlignment.Center,
                LineAlignment = StringAlignment.Center,
            };
            private readonly bool highResolutionTimer;
            private long frame;

            public TestPatternForm(Screen screen)
            {
                AutoScaleMode = AutoScaleMode.None;
                Bounds = screen.Bounds;
                FormBorderStyle = FormBorderStyle.None;
                StartPosition = FormStartPosition.Manual;
                ShowInTaskbar = false;
                TopMost = true;
                DoubleBuffered = true;
                highResolutionTimer = timeBeginPeriod(1) == 0;
                timer = new System.Windows.Forms.Timer { Interval = 16 };
                timer.Tick += (sender, args) =>
                {
                    frame = (long)(stopwatch.Elapsed.TotalSeconds * 60.0);
                    Invalidate();
                };
                Shown += (sender, args) => timer.Start();
            }

            protected override void OnPaint(PaintEventArgs args)
            {
                base.OnPaint(args);
                bool odd = (frame & 1) != 0;
                args.Graphics.Clear(odd ? Color.FromArgb(12, 26, 48) : Color.FromArgb(36, 12, 42));

                int bandWidth = Math.Max(ClientSize.Width / 12, 1);
                int movingBand = (int)(frame % 12) * bandWidth;
                using (Brush band = new SolidBrush(odd ? Color.Cyan : Color.Magenta))
                    args.Graphics.FillRectangle(band, movingBand, 0, bandWidth, ClientSize.Height);

                string text = String.Format(
                    CultureInfo.InvariantCulture,
                    "MiSTerCast E2E\nFRAME {0:D8}\n{1,8:F3} s",
                    frame,
                    stopwatch.Elapsed.TotalSeconds);
                args.Graphics.DrawString(text, counterFont, Brushes.White, ClientRectangle, centered);
            }

            protected override void Dispose(bool disposing)
            {
                if (disposing)
                {
                    timer.Dispose();
                    if (highResolutionTimer)
                        timeEndPeriod(1);
                    counterFont.Dispose();
                    centered.Dispose();
                }
                base.Dispose(disposing);
            }
        }

        private static readonly BlockingCollection<string> LogQueue = new BlockingCollection<string>();
        private static readonly ManualResetEventSlim ConnectionResult = new ManualResetEventSlim(false);
        private static readonly CancellationTokenSource Cancellation = new CancellationTokenSource();
        private static MiSTerCastInterop.LogDelegate logDelegate;
        private static MiSTerCastInterop.CaptureImageDelegate captureDelegate;
        private static int connectionState;

        [STAThread]
        private static int Main(string[] args)
        {
            try
            {
                return Run(ParseOptions(args));
            }
            catch (ArgumentException exception)
            {
                Console.Error.WriteLine(exception.Message);
                Console.Error.WriteLine("Use --help for usage.");
                return 2;
            }
            catch (Exception exception)
            {
                Console.Error.WriteLine(exception);
                return 1;
            }
        }

        private static int Run(Options options)
        {
            if (options.ShowHelp)
            {
                PrintUsage();
                return 0;
            }

            List<Modeline> modelines = ReadModelines(options.ModelinesPath);
            if (options.ListModelines)
            {
                foreach (Modeline modeline in modelines)
                    Console.WriteLine(modeline.Name);
                return 0;
            }

            IPAddress address;
            if (String.IsNullOrWhiteSpace(options.Target) ||
                !IPAddress.TryParse(options.Target, out address) ||
                address.AddressFamily != AddressFamily.InterNetwork)
            {
                throw new ArgumentException("--target must be a raw IPv4 address; host-name resolution is intentionally not used.");
            }

            Modeline selected = modelines.FirstOrDefault(
                candidate => String.Equals(candidate.Name, options.ModelineName, StringComparison.OrdinalIgnoreCase));
            if (selected == null)
                throw new ArgumentException("Unknown modeline: " + options.ModelineName);
            Modeline switched = null;
            if (!String.IsNullOrWhiteSpace(options.SwitchModelineName))
            {
                switched = modelines.FirstOrDefault(
                    candidate => String.Equals(candidate.Name, options.SwitchModelineName, StringComparison.OrdinalIgnoreCase));
                if (switched == null)
                    throw new ArgumentException("Unknown switch modeline: " + options.SwitchModelineName);
            }

            ushort captureWidth = options.CaptureWidth ?? selected.HActive;
            ushort captureHeight = options.CaptureHeight ?? selected.VActive;
            string logDirectory = Path.Combine(
                Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                "MiSTerCast",
                "Logs");
            Directory.CreateDirectory(logDirectory);
            string logPath = Path.Combine(
                logDirectory,
                "MiSTerCastCli-" + DateTime.Now.ToString("yyyyMMdd-HHmmss") + ".log");

            Task logTask = Task.Run(() => WriteLogs(logPath));
            logDelegate = LogCallback;
            captureDelegate = CaptureCallback;
            Console.CancelKeyPress += OnCancelKeyPress;

            bool initialized = false;
            bool streaming = false;
            TestPatternHost testPattern = null;
            try
            {
                QueueLog("CLI log: " + logPath, false);
                QueueLog(String.Format(
                    CultureInfo.InvariantCulture,
                    "E2E target={0} modeline=\"{1}\" switch_modeline=\"{2}\" duration={3}s display={4} audio={5} capture={6}x{7} test_pattern={8}",
                    address,
                    selected.Name,
                    switched == null ? "" : switched.Name,
                    options.DurationSeconds,
                    options.Display,
                    options.Audio,
                    captureWidth,
                    captureHeight,
                    options.TestPattern), false);

                if (options.TestPattern)
                {
                    testPattern = new TestPatternHost(options.Display);
                    testPattern.Start();
                    Thread.Sleep(500);
                }

                initialized = MiSTerCastInterop.Initialize(logDelegate, captureDelegate);
                if (!initialized)
                {
                    QueueLog("Native initialization failed.", true);
                    return 3;
                }

                MiSTerCastInterop.SetModeline(
                    selected.PixelClock,
                    selected.HActive,
                    selected.HBegin,
                    selected.HEnd,
                    selected.HTotal,
                    selected.VActive,
                    selected.VBegin,
                    selected.VEnd,
                    selected.VTotal,
                    selected.Interlace);
                MiSTerCastInterop.SetSource(
                    (byte)(options.Display - 1),
                    options.Audio,
                    false,
                    0,
                    0,
                    captureWidth,
                    captureHeight,
                    0,
                    0,
                    0);

                streaming = MiSTerCastInterop.StartStream(address.ToString());
                if (!streaming)
                {
                    QueueLog("Native stream start failed.", true);
                    return 4;
                }

                if (!ConnectionResult.Wait(TimeSpan.FromSeconds(5)) || Volatile.Read(ref connectionState) != 1)
                {
                    QueueLog("MiSTer did not acknowledge stream initialization.", true);
                    return 5;
                }

                DateTime started = DateTime.UtcNow;
                DateTime switchAt = started.AddSeconds(options.DurationSeconds / 2.0);
                DateTime end = started.AddSeconds(options.DurationSeconds);
                bool modeSwitched = false;
                while (!Cancellation.IsCancellationRequested && DateTime.UtcNow < end)
                {
                    if (!modeSwitched && switched != null && DateTime.UtcNow >= switchAt)
                    {
                        QueueLog("Switching live stream to modeline \"" + switched.Name + "\".", false);
                        MiSTerCastInterop.SetModeline(
                            switched.PixelClock,
                            switched.HActive,
                            switched.HBegin,
                            switched.HEnd,
                            switched.HTotal,
                            switched.VActive,
                            switched.VBegin,
                            switched.VEnd,
                            switched.VTotal,
                            switched.Interlace);
                        modeSwitched = true;
                    }
                    Thread.Sleep(50);
                }

                return 0;
            }
            finally
            {
                if (streaming)
                    MiSTerCastInterop.StopStream();
                if (initialized)
                    MiSTerCastInterop.Shutdown();
                testPattern?.Dispose();
                Console.CancelKeyPress -= OnCancelKeyPress;
                LogQueue.CompleteAdding();
                logTask.Wait();
            }
        }

        private static void LogCallback(string message, bool error)
        {
            QueueLog(message, error);
            if (String.Equals(message, "Done.", StringComparison.Ordinal))
            {
                Interlocked.Exchange(ref connectionState, 1);
                ConnectionResult.Set();
            }
            else if (message.IndexOf("Groovy MiSTer API failed to initialize", StringComparison.OrdinalIgnoreCase) >= 0)
            {
                Interlocked.Exchange(ref connectionState, -1);
                ConnectionResult.Set();
            }
        }

        private static void CaptureCallback(int width, int height, IntPtr buffer)
        {
        }

        private static void QueueLog(string message, bool error)
        {
            string line = String.Format(
                CultureInfo.InvariantCulture,
                "[{0:yyyy-MM-dd HH:mm:ss.fff}] [{1}] {2}",
                DateTime.Now,
                error ? "ERROR" : "INFO",
                message);
            LogQueue.TryAdd(line);
        }

        private static void WriteLogs(string path)
        {
            using (StreamWriter writer = new StreamWriter(
                new FileStream(path, FileMode.Create, FileAccess.Write, FileShare.ReadWrite),
                new UTF8Encoding(false)))
            {
                writer.AutoFlush = true;
                foreach (string line in LogQueue.GetConsumingEnumerable())
                {
                    Console.WriteLine(line);
                    writer.WriteLine(line);
                }
            }
        }

        private static void OnCancelKeyPress(object sender, ConsoleCancelEventArgs eventArgs)
        {
            eventArgs.Cancel = true;
            Cancellation.Cancel();
        }

        private static Options ParseOptions(string[] args)
        {
            Options options = new Options();
            for (int index = 0; index < args.Length; index++)
            {
                string argument = args[index];
                switch (argument)
                {
                case "--help":
                case "-h":
                    options.ShowHelp = true;
                    break;
                case "--list-modelines":
                    options.ListModelines = true;
                    break;
                case "--no-audio":
                    options.Audio = false;
                    break;
                case "--test-pattern":
                    options.TestPattern = true;
                    break;
                case "--target":
                    options.Target = NextValue(args, ref index, argument);
                    break;
                case "--modeline":
                    options.ModelineName = NextValue(args, ref index, argument);
                    break;
                case "--switch-modeline":
                    options.SwitchModelineName = NextValue(args, ref index, argument);
                    break;
                case "--modelines":
                    options.ModelinesPath = NextValue(args, ref index, argument);
                    break;
                case "--duration":
                    options.DurationSeconds = ParseInt(NextValue(args, ref index, argument), argument, 1, 86400);
                    break;
                case "--display":
                    options.Display = ParseInt(NextValue(args, ref index, argument), argument, 1, 4);
                    break;
                case "--capture-width":
                    options.CaptureWidth = (ushort)ParseInt(NextValue(args, ref index, argument), argument, 1, ushort.MaxValue);
                    break;
                case "--capture-height":
                    options.CaptureHeight = (ushort)ParseInt(NextValue(args, ref index, argument), argument, 1, ushort.MaxValue);
                    break;
                default:
                    throw new ArgumentException("Unknown argument: " + argument);
                }
            }
            return options;
        }

        private static string NextValue(string[] args, ref int index, string option)
        {
            if (++index >= args.Length)
                throw new ArgumentException("Missing value for " + option);
            return args[index];
        }

        private static int ParseInt(string value, string option, int minimum, int maximum)
        {
            int parsed;
            if (!Int32.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out parsed) ||
                parsed < minimum || parsed > maximum)
            {
                throw new ArgumentException(String.Format(
                    CultureInfo.InvariantCulture,
                    "{0} must be between {1} and {2}.",
                    option,
                    minimum,
                    maximum));
            }
            return parsed;
        }

        private static List<Modeline> ReadModelines(string path)
        {
            if (!File.Exists(path))
                throw new ArgumentException("Modeline file not found: " + path);

            List<Modeline> modelines = new List<Modeline>();
            foreach (string originalLine in File.ReadAllLines(path))
            {
                string line = originalLine.Trim();
                if (line.Length == 0 || line.StartsWith(";", StringComparison.Ordinal))
                    continue;

                int nameStart = line.IndexOf('[');
                int nameEnd = line.IndexOf(']');
                if (nameStart < 0 || nameEnd <= nameStart + 1)
                    continue;

                string[] values = line.Remove(nameStart, nameEnd - nameStart + 1)
                    .Split((char[])null, StringSplitOptions.RemoveEmptyEntries);
                if (values.Length != 10)
                    continue;

                double pixelClock;
                ushort hActive, hBegin, hEnd, hTotal, vActive, vBegin, vEnd, vTotal, interlace;
                if (!Double.TryParse(values[0], NumberStyles.Float, CultureInfo.InvariantCulture, out pixelClock) ||
                    !UInt16.TryParse(values[1], out hActive) ||
                    !UInt16.TryParse(values[2], out hBegin) ||
                    !UInt16.TryParse(values[3], out hEnd) ||
                    !UInt16.TryParse(values[4], out hTotal) ||
                    !UInt16.TryParse(values[5], out vActive) ||
                    !UInt16.TryParse(values[6], out vBegin) ||
                    !UInt16.TryParse(values[7], out vEnd) ||
                    !UInt16.TryParse(values[8], out vTotal) ||
                    !UInt16.TryParse(values[9], out interlace))
                {
                    continue;
                }

                modelines.Add(new Modeline
                {
                    Name = line.Substring(nameStart + 1, nameEnd - nameStart - 1),
                    PixelClock = pixelClock,
                    HActive = hActive,
                    HBegin = hBegin,
                    HEnd = hEnd,
                    HTotal = hTotal,
                    VActive = vActive,
                    VBegin = vBegin,
                    VEnd = vEnd,
                    VTotal = vTotal,
                    Interlace = interlace != 0,
                });
            }

            if (modelines.Count == 0)
                throw new ArgumentException("No valid modelines found in: " + path);
            return modelines;
        }

        private static void PrintUsage()
        {
            Console.WriteLine("MiSTerCastCli --target <IPv4> [options]");
            Console.WriteLine();
            Console.WriteLine("  --modeline <name>       Preset name from modelines.dat");
            Console.WriteLine("  --switch-modeline <name> Switch to another preset halfway through the run");
            Console.WriteLine("  --duration <seconds>    Streaming time (default: 10)");
            Console.WriteLine("  --display <1-4>         Windows display number (default: 1)");
            Console.WriteLine("  --capture-width <px>    Capture width (default: modeline active width)");
            Console.WriteLine("  --capture-height <px>   Capture height (default: modeline active height)");
            Console.WriteLine("  --no-audio              Disable loopback audio");
            Console.WriteLine("  --test-pattern          Show a full-screen moving frame counter on the captured display");
            Console.WriteLine("  --list-modelines        List preset names and exit");
            Console.WriteLine("  --modelines <path>      Use another modelines.dat file");
        }
    }
}
