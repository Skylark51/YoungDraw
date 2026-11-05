using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Security.Cryptography;
using System.Windows.Forms;
using Microsoft.Win32;

internal static class YoungDrawPatch
{
    private const string AppFile = "YoungDraw.exe";
    private const string Title = "YoungDraw 패치";

    [STAThread]
    private static int Main(string[] args)
    {
        try
        {
            string target = args.Length == 1 ? Path.GetFullPath(args[0]) : ChooseTarget(FindTargets());
            if (target == null) return 0;
            if (!IsTarget(target)) throw new IOException("선택한 경로에서 YoungDraw.exe를 찾을 수 없습니다.");

            while (IsRunning(target))
            {
                if (MessageBox.Show("열려 있는 YoungDraw에서 작업을 저장하고 프로그램을 닫은 뒤 다시 시도하세요.\n\n" + target,
                    Title, MessageBoxButtons.RetryCancel, MessageBoxIcon.Information) != DialogResult.Retry) return 0;
            }

            string backup;
            try { backup = Apply(target); }
            catch (UnauthorizedAccessException) when (args.Length == 0)
            {
                Process.Start(new ProcessStartInfo
                {
                    FileName = Application.ExecutablePath,
                    Arguments = "\"" + target + "\"",
                    Verb = "runas",
                    UseShellExecute = true
                });
                return 0;
            }

            string registration;
            try
            {
                RegisterOpenWith(target);
                registration = "탐색기의 ‘연결 프로그램’에서 YoungDraw를 선택할 수 있습니다.";
            }
            catch (Exception error)
            {
                registration = "YoungDraw의 파일 → 열기에서 CDXML을 열 수 있습니다. 연결 프로그램 등록 오류: " + error.Message;
            }
            string migration = "";
            if (MessageBox.Show(
                "이 컴퓨터의 로컬 고정 드라이브에서 기존 .ydraw 문서를 찾아 CDXML로 변환할까요?\n\n"
                + "변환된 CDXML은 ChemDraw와 YoungDraw에서 열 수 있습니다. 원본은 .ydraw.bak으로 보관됩니다. "
                + "같은 이름의 다른 CDXML은 덮어쓰지 않습니다.",
                Title, MessageBoxButtons.YesNo, MessageBoxIcon.Information) == DialogResult.Yes)
            {
                Cursor.Current = Cursors.WaitCursor;
                try { migration = "\n\n" + MigrateDrawings(target); }
                catch (Exception error) { migration = "\n\n문서 변환이 중단되었습니다. " + error.Message; }
                finally { Cursor.Current = Cursors.Default; }
            }

            MessageBox.Show((backup == null ? "이미 이 패치가 적용되어 있습니다.\n\n" + target
                : "패치를 적용했습니다.\n\n" + target + "\n\n이전 버전 백업\n" + backup)
                + "\n\nCDXML은 ChemDraw와 YoungDraw에서 열 수 있습니다.\n" + registration + migration,
                Title, MessageBoxButtons.OK, MessageBoxIcon.Information);
            return 0;
        }
        catch (System.ComponentModel.Win32Exception error) when (error.NativeErrorCode == 1223)
        {
            return 0;
        }
        catch (Exception error)
        {
            MessageBox.Show("패치를 적용할 수 없습니다.\n\n" + error.Message,
                Title, MessageBoxButtons.OK, MessageBoxIcon.Error);
            return 1;
        }
    }

    private static void RegisterOpenWith(string target)
    {
        const string progId = "YoungDraw.CDXML";
        using (var classes = Registry.CurrentUser.CreateSubKey(@"Software\Classes"))
        using (var fileType = classes.CreateSubKey(progId))
        using (var command = fileType.CreateSubKey(@"shell\open\command"))
        using (var choices = classes.CreateSubKey(@".cdxml\OpenWithProgids"))
        {
            fileType.SetValue(null, "YoungDraw");
            fileType.SetValue("FriendlyTypeName", "YoungDraw CDXML");
            command.SetValue(null, "\"" + target + "\" \"%1\"");
            choices.SetValue(progId, new byte[0], RegistryValueKind.None);
        }
    }

    private static string MigrateDrawings(string target)
    {
        string log = Path.Combine(Path.GetTempPath(), "YoungDraw-CDXML-" + Guid.NewGuid().ToString("N") + ".log");
        int converted = 0, failed = 0, skippedFolders = 0;
        var folders = new Stack<string>(DriveInfo.GetDrives()
            .Where(drive => drive.DriveType == DriveType.Fixed && drive.IsReady)
            .Select(drive => drive.RootDirectory.FullName));
        using (var report = new StreamWriter(log))
        {
            report.AutoFlush = true;
            while (folders.Count != 0)
            {
                string folder = folders.Pop();
                try
                {
                    foreach (string path in Directory.EnumerateFileSystemEntries(folder))
                    {
                        try
                        {
                            var attributes = File.GetAttributes(path);
                            if ((attributes & FileAttributes.ReparsePoint) != 0) continue;
                            if ((attributes & FileAttributes.Directory) != 0)
                            {
                                if (Path.GetFileName(path) != ".git") folders.Push(path);
                                continue;
                            }
                            if (!String.Equals(Path.GetExtension(path), ".ydraw", StringComparison.OrdinalIgnoreCase)) continue;
                            using (var process = Process.Start(new ProcessStartInfo
                            {
                                FileName = target,
                                Arguments = "--migrate-ydraw \"" + path + "\"",
                                UseShellExecute = false,
                                CreateNoWindow = true
                            }))
                            {
                                process.WaitForExit();
                                if (process.ExitCode != 0) throw new IOException("CDXML 변환 또는 원본 백업 실패");
                            }
                            converted++;
                            report.WriteLine("변환 완료\t" + path);
                        }
                        catch (Exception error) when (error is IOException || error is UnauthorizedAccessException || error is System.ComponentModel.Win32Exception)
                        {
                            failed++;
                            report.WriteLine("처리 실패\t" + path + "\t" + error.Message);
                        }
                    }
                }
                catch (Exception error) when (error is IOException || error is UnauthorizedAccessException)
                {
                    skippedFolders++;
                    report.WriteLine("검색 실패\t" + folder + "\t" + error.Message);
                }
            }
        }
        return String.Format("CDXML 변환 {0}개 · 처리 실패 {1}개 · 접근할 수 없는 폴더 {2}개\n변환 기록\n{3}",
            converted, failed, skippedFolders, log);
    }

    private static bool IsTarget(string path)
    {
        return String.Equals(Path.GetFileName(path), AppFile, StringComparison.OrdinalIgnoreCase) && File.Exists(path);
    }

    private static IEnumerable<string> RunningPaths()
    {
        foreach (var process in Process.GetProcessesByName("YoungDraw"))
        {
            using (process)
            {
                string path = null;
                try { path = process.MainModule.FileName; }
                catch (System.ComponentModel.Win32Exception) { }
                catch (InvalidOperationException) { }
                if (path != null) yield return path;
            }
        }
    }

    private static bool IsRunning(string path)
    {
        return RunningPaths().Contains(path, StringComparer.OrdinalIgnoreCase);
    }

    private static void AddTarget(HashSet<string> targets, string path)
    {
        if (String.IsNullOrWhiteSpace(path)) return;
        path = Environment.ExpandEnvironmentVariables(path.Trim().Trim('"'));
        if (Path.IsPathRooted(path) && IsTarget(path)) targets.Add(Path.GetFullPath(path));
    }

    private static void FindRegisteredTargets(HashSet<string> targets)
    {
        foreach (var hive in new[] { RegistryHive.CurrentUser, RegistryHive.LocalMachine })
        foreach (var view in new[] { RegistryView.Registry64, RegistryView.Registry32 })
        {
            using (var root = RegistryKey.OpenBaseKey(hive, view))
            using (var appPath = root.OpenSubKey(@"SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\" + AppFile))
            using (var uninstall = root.OpenSubKey(@"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall"))
            {
                if (appPath != null) AddTarget(targets, appPath.GetValue(null) as string);
                if (uninstall == null) continue;
                foreach (var name in uninstall.GetSubKeyNames())
                using (var entry = uninstall.OpenSubKey(name))
                {
                    if (entry == null) continue;
                    var display = entry.GetValue("DisplayName") as string;
                    var folder = entry.GetValue("InstallLocation") as string;
                    if (display != null && display.StartsWith("YoungDraw", StringComparison.OrdinalIgnoreCase)
                        && !String.IsNullOrWhiteSpace(folder))
                        AddTarget(targets, Path.Combine(folder.Trim().Trim('"'), AppFile));
                }
            }
        }
    }

    private static IEnumerable<string> FindFiles(string folder, string pattern, int depth)
    {
        if (!Directory.Exists(folder)) yield break;
        string[] files;
        string[] children;
        try
        {
            files = Directory.GetFiles(folder, pattern);
            children = depth == 0 ? new string[0] : Directory.GetDirectories(folder);
        }
        catch (IOException) { yield break; }
        catch (UnauthorizedAccessException) { yield break; }

        foreach (string file in files) yield return file;
        foreach (string child in children)
        {
            FileAttributes attributes;
            try { attributes = File.GetAttributes(child); }
            catch (IOException) { continue; }
            catch (UnauthorizedAccessException) { continue; }
            if ((attributes & FileAttributes.ReparsePoint) != 0) continue;
            foreach (string file in FindFiles(child, pattern, depth - 1)) yield return file;
        }
    }

    private static List<string> FindTargets()
    {
        var targets = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (string path in RunningPaths()) AddTarget(targets, path);
        AddTarget(targets, Path.Combine(AppDomain.CurrentDomain.BaseDirectory, AppFile));
        try { FindRegisteredTargets(targets); }
        catch (UnauthorizedAccessException) { }
        catch (System.Security.SecurityException) { }

        var shortcutFolders = new[] { Environment.SpecialFolder.DesktopDirectory, Environment.SpecialFolder.CommonDesktopDirectory,
            Environment.SpecialFolder.Programs, Environment.SpecialFolder.CommonPrograms };
        try
        {
            Type shellType = Type.GetTypeFromProgID("WScript.Shell");
            if (shellType != null)
            {
                dynamic shell = Activator.CreateInstance(shellType);
                foreach (var folder in shortcutFolders)
                foreach (var link in FindFiles(Environment.GetFolderPath(folder), "*.lnk", 4))
                {
                    try { AddTarget(targets, (string)shell.CreateShortcut(link).TargetPath); }
                    catch (System.Runtime.InteropServices.COMException) { }
                    catch (ArgumentException) { }
                }
            }
        }
        catch (System.Runtime.InteropServices.COMException) { }

        if (targets.Count == 0)
        {
            foreach (var folder in new[] { Environment.SpecialFolder.DesktopDirectory, Environment.SpecialFolder.MyDocuments,
                Environment.SpecialFolder.ProgramFiles, Environment.SpecialFolder.ProgramFilesX86 })
            foreach (var path in FindFiles(Environment.GetFolderPath(folder), AppFile, 4))
                AddTarget(targets, path);
            foreach (var folder in new[] { Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Programs"),
                Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), "Downloads") })
            foreach (var path in FindFiles(folder, AppFile, 4))
                AddTarget(targets, path);
        }
        return targets.OrderBy(path => path, StringComparer.OrdinalIgnoreCase).ToList();
    }

    private static string ChooseTarget(List<string> targets)
    {
        if (targets.Count == 1) return targets[0];
        if (targets.Count == 0) return Browse();

        using (var dialog = new Form { Text = Title, Width = 850, Height = 280, StartPosition = FormStartPosition.CenterScreen })
        using (var paths = new ListBox { Dock = DockStyle.Fill, HorizontalScrollbar = true })
        using (var label = new Label { Text = "YoungDraw가 여러 곳에 있습니다. 패치할 실행 파일을 선택하세요.", Dock = DockStyle.Top, Height = 32 })
        using (var buttons = new FlowLayoutPanel { Dock = DockStyle.Bottom, Height = 42, FlowDirection = FlowDirection.RightToLeft })
        using (var apply = new Button { Text = "패치 적용", DialogResult = DialogResult.OK, Enabled = false, AutoSize = true })
        using (var browse = new Button { Text = "다른 위치…", DialogResult = DialogResult.Retry, AutoSize = true })
        using (var cancel = new Button { Text = "취소", DialogResult = DialogResult.Cancel, AutoSize = true })
        {
            paths.Items.AddRange(targets.ToArray());
            paths.SelectedIndexChanged += (sender, args) => apply.Enabled = paths.SelectedIndex >= 0;
            buttons.Controls.AddRange(new Control[] { cancel, apply, browse });
            dialog.Controls.AddRange(new Control[] { paths, buttons, label });
            dialog.AcceptButton = apply;
            dialog.CancelButton = cancel;
            var result = dialog.ShowDialog();
            return result == DialogResult.OK ? paths.SelectedItem as string : result == DialogResult.Retry ? Browse() : null;
        }
    }

    private static string Browse()
    {
        using (var picker = new OpenFileDialog { Title = "패치할 기존 YoungDraw.exe를 선택하세요",
            Filter = "YoungDraw 실행 파일|YoungDraw.exe", FileName = AppFile, CheckFileExists = true })
            return picker.ShowDialog() == DialogResult.OK ? picker.FileName : null;
    }

    private static string Apply(string target)
    {
        string token = Guid.NewGuid().ToString("N");
        string stage = target + "." + token + ".tmp";
        string backup = target + "." + DateTime.Now.ToString("yyyyMMdd-HHmmss") + "-" + token.Substring(0, 8) + ".bak";
        try
        {
            using (var payload = Assembly.GetExecutingAssembly().GetManifestResourceStream(AppFile))
            using (var sha = SHA256.Create())
            {
                if (payload == null) throw new IOException("패치에 YoungDraw 실행 파일이 없습니다.");
                byte[] currentHash;
                using (var current = File.OpenRead(target)) currentHash = sha.ComputeHash(current);
                if (currentHash.SequenceEqual(sha.ComputeHash(payload))) return null;
                payload.Position = 0;
                using (var output = new FileStream(stage, FileMode.CreateNew, FileAccess.Write, FileShare.None))
                {
                    payload.CopyTo(output);
                    output.Flush(true);
                }
            }
            File.Replace(stage, target, backup);
            return backup;
        }
        finally
        {
            if (File.Exists(stage)) File.Delete(stage);
        }
    }
}
