// Pokémon FireRed PSP Builder: builds the game from the user's own ROM on
// Windows. The PSP toolchain only runs on Linux and macOS, so the build runs
// in Windows' own Linux part (WSL) with Ubuntu: the app installs the build
// tools there, gets this repository, and runs make_psp.sh, the same builder
// Linux users run. It shows make_psp.sh's steps as a progress bar, keeps the
// result next to the app, and copies it to a PSP in USB mode if one is there.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Security.Cryptography;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace FireRedPSPBuilder
{
    static class Program
    {
        // FireRed PSP Builder.exe --test ROM LOG: builds ROM straight away with no
        // questions, writes the details to LOG and exits with 0 if it worked
        // (for trying the app out unattended)
        [STAThread]
        static int Main(string[] args)
        {
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            var form = args.Length == 3 && args[0] == "--test" ? new BuilderForm(args[1], args[2]) : new BuilderForm(null, null);
            Application.Run(form);
            return form.ExitCode;
        }
    }

    class BuilderForm : Form
    {
        // The Ubuntu WSL installs ("Ubuntu"), or one that's already there under
        // another name, like the Microsoft Store's "Ubuntu-24.04"
        const string NewDistro = "Ubuntu";
        string distro = NewDistro;
        const string RepoUrl = "https://github.com/JustKidding23/Pokemon-FireRed-PSP.git";
        const string RepoDir = "~/Pokemon-FireRed-PSP";
        // Pokémon FireRed (USA), versions 1.0 and 1.1
        static readonly string[] FireRedSha1 =
        {
            "41cb23d8dccc8ebd7c649cd8fbb58eeace6e2fdc",
            "dd5945db9b930750cb39d00c84da8571feebf417",
        };
        const string Packages = "build-essential git libpng-dev python3 python3-venv curl cmake ffmpeg libsndfile1-dev gcc-multilib";

        // make_psp.sh's steps ("== ..." lines), and how far along each one is
        static readonly (string Step, int Percent, string Text)[] BuildSteps =
        {
            ("Checking your ROM", 25, "Checking your ROM..."),
            ("Checking the build tools", 27, "Checking the build tools..."),
            ("Getting the PSP toolchain", 30, "Getting the PSP's tools (a few hundred MB, the first time)..."),
            ("Building the game", 45, "Building the game (the first time takes a while)..."),
            ("Making the PSP menu", 85, "Making the PSP menu's icon, picture and music..."),
            ("Done", 95, "Saving the game..."),
        };

        readonly string outputDir = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "FireRed PSP");
        readonly Button chooseButton = new Button();
        readonly Label statusLabel = new Label();
        readonly ProgressBar progressBar = new ProgressBar();
        readonly Button copyButton = new Button();
        readonly Button openButton = new Button();
        readonly CheckBox detailsBox = new CheckBox();
        readonly TextBox logBox = new TextBox();
        readonly Timer pspTimer = new Timer();
        readonly StringBuilder log = new StringBuilder();
        bool built;
        // The test mode's ROM and log (see Main)
        readonly string testRom, testLog;
        public int ExitCode { get; private set; }

        public BuilderForm(string testRom, string testLog)
        {
            this.testRom = testRom;
            this.testLog = testLog;
            Text = "Pokémon FireRed PSP Builder";
            Font = new Font("Segoe UI", 10f);
            FormBorderStyle = FormBorderStyle.FixedSingle;
            MaximizeBox = false;
            StartPosition = FormStartPosition.CenterScreen;
            AutoScaleMode = AutoScaleMode.Dpi;
            ClientSize = new Size(480, 250);

            var title = new Label
            {
                Text = "Pokémon FireRed PSP",
                Font = new Font("Segoe UI", 14f, FontStyle.Bold),
                AutoSize = true,
                Location = new Point(20, 16),
            };
            var intro = new Label
            {
                Text = "Builds the game from your own Pokémon FireRed (USA) ROM.",
                AutoSize = true,
                Location = new Point(22, 50),
            };
            chooseButton.Text = "Choose your FireRed ROM...";
            chooseButton.Size = new Size(260, 36);
            chooseButton.Location = new Point(20, 82);
            chooseButton.Click += async (s, e) => await ChooseAndBuild();

            statusLabel.AutoSize = false;
            statusLabel.Size = new Size(440, 24);
            statusLabel.Location = new Point(20, 130);
            progressBar.Size = new Size(440, 20);
            progressBar.Location = new Point(20, 156);

            copyButton.Text = "Copy to PSP";
            copyButton.Size = new Size(210, 34);
            copyButton.Location = new Point(20, 190);
            copyButton.Enabled = false;
            copyButton.Click += (s, e) => CopyToPsp();
            openButton.Text = "Open folder";
            openButton.Size = new Size(120, 34);
            openButton.Location = new Point(240, 190);
            openButton.Enabled = false;
            openButton.Click += (s, e) => Process.Start("explorer.exe", "\"" + outputDir + "\"");

            detailsBox.Text = "Show details";
            detailsBox.AutoSize = true;
            detailsBox.Location = new Point(370, 197);
            detailsBox.CheckedChanged += (s, e) =>
            {
                logBox.Visible = detailsBox.Checked;
                ClientSize = new Size(480, detailsBox.Checked ? 470 : 250);
            };
            logBox.Multiline = true;
            logBox.ReadOnly = true;
            logBox.ScrollBars = ScrollBars.Vertical;
            logBox.Font = new Font("Consolas", 9f);
            logBox.Size = new Size(440, 200);
            logBox.Location = new Point(20, 240);
            logBox.Visible = false;

            Controls.AddRange(new Control[] { title, intro, chooseButton, statusLabel, progressBar, copyButton, openButton, detailsBox, logBox });

            // The PSP can be plugged in after the build: look for it every 2 seconds
            pspTimer.Interval = 2000;
            pspTimer.Tick += (s, e) => UpdateCopyButton();
            pspTimer.Start();
            SetStatus("", 0);
            if (testRom != null)
            {
                detailsBox.Checked = true;
                Shown += async (s, e) =>
                {
                    ExitCode = await BuildRom(testRom) ? 0 : 1;
                    try
                    {
                        File.WriteAllText(testLog, log.ToString() + Environment.NewLine + "Status: " + statusLabel.Text + Environment.NewLine);
                    }
                    catch (IOException)
                    {
                    }
                    Close();
                };
            }
        }

        void SetStatus(string text, int percent)
        {
            statusLabel.Text = text;
            progressBar.Value = Math.Max(0, Math.Min(100, percent));
        }

        void AddLog(string line)
        {
            log.AppendLine(line);
            logBox.AppendText(line + Environment.NewLine);
        }

        async Task ChooseAndBuild()
        {
            using (var dialog = new OpenFileDialog { Title = "Choose your Pokémon FireRed ROM", Filter = "GBA ROM (*.gba)|*.gba|All files (*.*)|*.*" })
            {
                if (dialog.ShowDialog(this) == DialogResult.OK)
                    await BuildRom(dialog.FileName);
            }
        }

        async Task<bool> BuildRom(string rom)
        {
            if (!IsFireRed(rom))
            {
                if (testRom == null)
                    MessageBox.Show(this, "That isn't Pokémon FireRed (USA), version 1.0 or 1.1.\n\nUse a copy of your own cartridge, dumped unchanged.",
                                    Text, MessageBoxButtons.OK, MessageBoxIcon.Warning);
                SetStatus("That isn't Pokémon FireRed (USA).", 0);
                return false;
            }

            chooseButton.Enabled = false;
            built = false;
            log.Clear();
            logBox.Clear();
            try
            {
                if (await Build(rom))
                {
                    built = true;
                    SetStatus("Done! The game is in the \"FireRed PSP\" folder next to this app.", 100);
                    openButton.Enabled = true;
                    UpdateCopyButton();
                    if (FindPsp() != null && testRom == null)
                        CopyToPsp();
                    return true;
                }
            }
            catch (Exception ex)
            {
                AddLog(ex.ToString());
                Failed("Something went wrong: " + ex.Message);
            }
            finally
            {
                chooseButton.Enabled = true;
                SaveLog();
            }
            return false;
        }

        static bool IsFireRed(string path)
        {
            using (var sha = SHA1.Create())
            using (var file = File.OpenRead(path))
            {
                string sum = string.Concat(sha.ComputeHash(file).Select(b => b.ToString("x2")));
                return FireRedSha1.Contains(sum);
            }
        }

        async Task<bool> Build(string rom)
        {
            SetStatus("Checking Windows' Linux part (WSL)...", 2);
            if (!await HasUbuntu())
            {
                if (testRom == null)
                    OfferWslInstall();
                else
                    AddLog("No Ubuntu in WSL");
                SetStatus("", 0);
                return false;
            }

            SetStatus("Installing the build tools in Ubuntu...", 8);
            if (await RunWsl(true, "export DEBIAN_FRONTEND=noninteractive && apt-get update -q && apt-get install -y -q " + Packages, null) != 0)
                return Failed("The build tools couldn't be installed. Is the PC connected to the internet?");

            SetStatus("Getting Pokémon FireRed PSP...", 20);
            if (await RunWsl(true, "cd ~ && if [ -d " + RepoDir + "/.git ]; then git -C " + RepoDir + " pull -q --ff-only; " +
                                   "else git clone -q --depth 1 " + RepoUrl + " " + RepoDir + "; fi", null) != 0)
                return Failed("Pokémon FireRed PSP couldn't be downloaded from GitHub.");

            Directory.CreateDirectory(outputDir);
            int status = await RunWsl(true, "cd " + RepoDir + " && ./make_psp.sh " + WslPath(rom), line =>
            {
                foreach (var step in BuildSteps)
                {
                    if (line.StartsWith("== " + step.Step))
                        SetStatus(step.Text, step.Percent);
                }
            });
            if (status != 0)
                return Failed("The build stopped. \"Show details\" says why.");

            // The result, next to the app (the old one goes first)
            string psp = Path.Combine(outputDir, "PSP");
            if (Directory.Exists(psp))
                Directory.Delete(psp, true);
            if (await RunWsl(true, "cp -r " + RepoDir + "/out/PSP " + WslPath(outputDir) + "/", null) != 0)
                return Failed("The game couldn't be saved next to the app.");
            return File.Exists(Path.Combine(psp, "GAME", "FIRERED_PSP", "EBOOT.PBP"))
                || Failed("The game wasn't where it should be after the build.");
        }

        bool Failed(string message)
        {
            SetStatus(message, progressBar.Value);
            detailsBox.Checked = true;
            return false;
        }

        // A Windows path as Ubuntu sees it, quoted for bash
        static string WslPath(string windowsPath)
        {
            return "\"$(wslpath -u '" + windowsPath.Replace("'", "'\\''") + "')\"";
        }

        static string WslExe()
        {
            return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.System), "wsl.exe");
        }

        async Task<bool> HasUbuntu()
        {
            if (!File.Exists(WslExe()))
                return false;
            // wsl.exe writes its own messages in UTF-16
            var lines = new List<string>();
            int status = await Run(WslExe(), "--list --quiet", Encoding.Unicode, lines.Add);
            var names = lines.Select(l => l.Trim().Trim('\0')).ToList();
            string found = names.FirstOrDefault(n => n == NewDistro)
                        ?? names.FirstOrDefault(n => n.StartsWith(NewDistro + "-", StringComparison.OrdinalIgnoreCase));
            if (status != 0 || found == null)
                return false;
            distro = found;
            return true;
        }

        void OfferWslInstall()
        {
            var answer = MessageBox.Show(this,
                "To build the game, Windows needs its Linux part (WSL) with Ubuntu. Install them now?\n\n" +
                "Windows will ask for permission, and you may need to restart the PC. Then Ubuntu opens once " +
                "and asks you to choose a user name and password. After that, run this app again.",
                Text, MessageBoxButtons.YesNo, MessageBoxIcon.Information);
            if (answer != DialogResult.Yes)
                return;
            try
            {
                // --web-download: without the Microsoft Store, which some PCs don't have
                Process.Start(new ProcessStartInfo(WslExe(), "--install --web-download -d " + NewDistro) { Verb = "runas", UseShellExecute = true });
            }
            catch (Exception ex)
            {
                MessageBox.Show(this, "WSL couldn't be installed: " + ex.Message, Text, MessageBoxButtons.OK, MessageBoxIcon.Warning);
            }
        }

        // A command in Ubuntu, as root (so nothing asks for a password), its
        // output going to the details and to onLine
        Task<int> RunWsl(bool asRoot, string command, Action<string> onLine)
        {
            // --exec runs bash directly, so the command is read once, by bash
            // (without it, Ubuntu's shell would read it first)
            string args = "-d " + distro + (asRoot ? " -u root" : "") + " --exec bash -lc " + QuoteArgument(command);
            AddLog("> " + command);
            return Run(WslExe(), args, new UTF8Encoding(false), line =>
            {
                AddLog(line);
                onLine?.Invoke(line);
            });
        }

        // One argument for a Windows command line: in quotes, a quote escaped
        // with a backslash, and backslashes doubled only before a quote, as
        // Windows programs read it
        static string QuoteArgument(string arg)
        {
            var quoted = new StringBuilder("\"");
            int backslashes = 0;

            foreach (char c in arg)
            {
                if (c == '\\')
                {
                    backslashes++;
                    continue;
                }
                if (c == '"')
                    quoted.Append('\\', backslashes * 2 + 1);
                else
                    quoted.Append('\\', backslashes);
                quoted.Append(c);
                backslashes = 0;
            }
            quoted.Append('\\', backslashes * 2);
            return quoted.Append('"').ToString();
        }

        Task<int> Run(string exe, string args, Encoding encoding, Action<string> onLine)
        {
            var done = new TaskCompletionSource<int>();
            var info = new ProcessStartInfo(exe, args)
            {
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                StandardOutputEncoding = encoding,
                StandardErrorEncoding = encoding,
            };
            var process = new Process { StartInfo = info, EnableRaisingEvents = true };
            DataReceivedEventHandler handler = (s, e) =>
            {
                // curl's progress bar redraws one line with \r
                if (e.Data != null)
                    BeginInvoke(new Action(() => onLine(e.Data.Split('\r').Last())));
            };
            process.OutputDataReceived += handler;
            process.ErrorDataReceived += handler;
            process.Exited += (s, e) =>
            {
                process.WaitForExit();   // the last lines of output first
                BeginInvoke(new Action(() => done.SetResult(process.ExitCode)));
            };
            process.Start();
            process.BeginOutputReadLine();
            process.BeginErrorReadLine();
            return done.Task;
        }

        void SaveLog()
        {
            try
            {
                Directory.CreateDirectory(outputDir);
                File.WriteAllText(Path.Combine(outputDir, "build-log.txt"), log.ToString());
            }
            catch (IOException)
            {
            }
        }

        // A PSP in USB mode: a removable drive with a PSP folder
        static DriveInfo FindPsp()
        {
            return DriveInfo.GetDrives().FirstOrDefault(d =>
            {
                try
                {
                    return d.DriveType == DriveType.Removable && d.IsReady && Directory.Exists(Path.Combine(d.RootDirectory.FullName, "PSP"));
                }
                catch (IOException)
                {
                    return false;
                }
            });
        }

        void UpdateCopyButton()
        {
            var psp = built ? FindPsp() : null;

            copyButton.Enabled = psp != null;
            copyButton.Text = psp != null ? "Copy to PSP (" + psp.Name.TrimEnd('\\') + ")"
                            : built ? "Copy to PSP (connect it in USB mode)" : "Copy to PSP";
        }

        // Only the game's own files: the saves and settings on the PSP stay
        void CopyToPsp()
        {
            var psp = FindPsp();

            if (psp == null)
                return;
            var answer = MessageBox.Show(this, "Copy Pokémon FireRed PSP to the PSP (" + psp.Name.TrimEnd('\\') + ")?\n\nYour saves on the PSP aren't touched.",
                                         Text, MessageBoxButtons.YesNo, MessageBoxIcon.Question);
            if (answer != DialogResult.Yes)
                return;
            try
            {
                string from = Path.Combine(outputDir, "PSP", "GAME", "FIRERED_PSP");
                string to = Path.Combine(psp.RootDirectory.FullName, "PSP", "GAME", "FIRERED_PSP");

                Directory.CreateDirectory(to);
                foreach (string file in Directory.GetFiles(from))
                    File.Copy(file, Path.Combine(to, Path.GetFileName(file)), true);
                MessageBox.Show(this, "Done! Eject the PSP in Windows before you unplug it. The game is in the PSP's Game menu.",
                                Text, MessageBoxButtons.OK, MessageBoxIcon.Information);
            }
            catch (Exception ex)
            {
                MessageBox.Show(this, "The game couldn't be copied to the PSP: " + ex.Message, Text, MessageBoxButtons.OK, MessageBoxIcon.Warning);
            }
        }
    }
}
