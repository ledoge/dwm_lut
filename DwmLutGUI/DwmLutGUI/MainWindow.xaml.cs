using System;
using System.Diagnostics;
using System.Drawing;
using System.Linq;
using System.Reflection;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Forms;
using MessageBox = System.Windows.Forms.MessageBox;

namespace DwmLutGUI
{
    public partial class MainWindow
    {
        private readonly MainViewModel _viewModel;
        private bool _applyOnCooldown;

        private readonly MenuItem _statusItem;
        private readonly MenuItem _applyItem;
        private readonly MenuItem _disableItem;
        private readonly MenuItem _disableAndExitItem;

        public MainWindow()
        {
            if (Process.GetProcessesByName(Process.GetCurrentProcess().ProcessName).Length > 1)
            {
                MessageBox.Show("Already running!");
                Close();
                return;
            }

            InitializeComponent();
            _viewModel = (MainViewModel)DataContext;
            _applyOnCooldown = false;

            var args = Environment.GetCommandLineArgs().ToList();
            args.RemoveAt(0);

            bool exitImmediately = args.Contains("-exit");

            if (args.Contains("-apply"))
            {
                if (exitImmediately)
                {
                    ArgLutPath(args, "-sdr", (MonitorData m, string value) => m.SdrLutPath = value);
                    ArgLutPath(args, "-hdr", (MonitorData m, string value) => m.HdrLutPath = value);
                }

                Apply_Click(null, null);
            }
            else if (args.Contains("-disable"))
            {
                Disable_Click(null, null);
            }

            if (args.Contains("-minimize"))
            {
                WindowState = WindowState.Minimized;
                Hide();
            }
            else if (exitImmediately)
            {
                Close();
                return;
            }

            var notifyIcon = new NotifyIcon();
            var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream("DwmLutGUI.smile.ico");
            notifyIcon.Icon = new Icon(stream);
            notifyIcon.Visible = true;
            notifyIcon.DoubleClick +=
                delegate
                {
                    Show();
                    WindowState = WindowState.Normal;
                };

            var contextMenu = new ContextMenu();

            _statusItem = new MenuItem();
            contextMenu.MenuItems.Add(_statusItem);
            _statusItem.Enabled = false;

            contextMenu.MenuItems.Add("-");

            _applyItem = new MenuItem();
            contextMenu.MenuItems.Add(_applyItem);
            _applyItem.Text = "Apply";
            _applyItem.Click += delegate { Apply_Click(null, null); };

            _disableItem = new MenuItem();
            contextMenu.MenuItems.Add(_disableItem);
            _disableItem.Text = "Disable";
            _disableItem.Click += delegate { Disable_Click(null, null); };

            contextMenu.MenuItems.Add("-");

            _disableAndExitItem = new MenuItem();
            contextMenu.MenuItems.Add(_disableAndExitItem);
            _disableAndExitItem.Text = "Disable and exit";
            _disableAndExitItem.Click += delegate
            {
                Disable_Click(null, null);
                Close();
            };

            var exitItem = new MenuItem();
            contextMenu.MenuItems.Add(exitItem);
            exitItem.Text = "Exit";
            exitItem.Click += delegate { Close(); };

            contextMenu.Popup += delegate { UpdateContextMenu(); };

            notifyIcon.ContextMenu = contextMenu;

            notifyIcon.Text = Assembly.GetEntryAssembly().GetName().Name;

            Closed += delegate { notifyIcon.Dispose(); };
        }

        protected override void OnStateChanged(EventArgs e)
        {
            if (WindowState == WindowState.Minimized)
            {
                Hide();
            }

            base.OnStateChanged(e);
        }

        private void UpdateContextMenu()
        {
            _statusItem.Text = "Status: " + _viewModel.ActiveText;

            var canDisable = _viewModel.IsActive && !Injector.NoDebug;

            _applyItem.Enabled = _viewModel.CanApply;
            _disableItem.Enabled = canDisable;
            _disableAndExitItem.Enabled = canDisable;
        }

        private static string BrowseLuts()
        {
            var dlg = new Microsoft.Win32.OpenFileDialog
            {
                Filter = "LUT Files|*.cube;*.txt"
            };

            var result = dlg.ShowDialog();

            return result == true ? dlg.FileName : null;
        }

        private void MonitorRefreshButton_Click(object sender, RoutedEventArgs e)
        {
            _viewModel.UpdateMonitors();
        }

        private void SdrLutBrowse_Click(object sender, RoutedEventArgs e)
        {
            var lutPath = BrowseLuts();
            if (!string.IsNullOrEmpty(lutPath))
            {
                _viewModel.SdrLutPath = lutPath;
            }
        }

        private void SdrLutClear_Click(object sender, RoutedEventArgs e)
        {
            _viewModel.SdrLutPath = null;
        }

        private void HdrLutBrowse_Click(object sender, RoutedEventArgs e)
        {
            var lutPath = BrowseLuts();
            if (!string.IsNullOrEmpty(lutPath))
            {
                _viewModel.HdrLutPath = lutPath;
            }
        }

        private void HdrLutClear_Click(object sender, RoutedEventArgs e)
        {
            _viewModel.HdrLutPath = null;
        }

        private void Disable_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                _viewModel.Uninject();
                RedrawScreens();
            }
            catch (Exception x)
            {
                MessageBox.Show(x.Message);
            }
        }

        private void Apply_Click(object sender, RoutedEventArgs e)
        {
            if (_applyOnCooldown) return;
            _applyOnCooldown = true;

            try
            {
                _viewModel.ReInject();
                RedrawScreens();
            }
            catch (Exception x)
            {
                MessageBox.Show(x.Message);
            }

            Task.Run(() =>
            {
                Thread.Sleep(100);
                _applyOnCooldown = false;
            });
        }

        private void ArgLutPath(System.Collections.Generic.IList<string> args, string argName, Action<MonitorData, string> callback)
        {
            int argIndex = args.IndexOf(argName);
            if (argIndex == -1)
            {
                return;
            }

            string argValue = args.ElementAtOrDefault(argIndex + 1);
            if (argValue == null)
            {
                return;
            }

            char[] monitorValueSplitter = { ':' };
            foreach (string monitorIndexToValue in argValue.Split(';'))
            {
                string[] splitted = monitorIndexToValue.Split(monitorValueSplitter, 2);
                if (int.TryParse(splitted[0], out int monitorIndex))
                {
                    monitorIndex -= 1;
                }
                else
                {
                    continue;
                }

                MonitorData monitorObject = _viewModel.Monitors.ElementAtOrDefault(monitorIndex);
                if (monitorObject == null)
                {
                    continue;
                }

                string pristineValue = splitted.ElementAtOrDefault(1);
                if (pristineValue == null)
                {
                    continue;
                }

                string trimmedValue = pristineValue.Trim();
                if (trimmedValue != "")
                {
                    callback(monitorObject, trimmedValue);
                }
            }
        }

        private static void RedrawScreens()
        {
            var rect = Screen.AllScreens.Select(x => x.Bounds).Aggregate(Rectangle.Union);
            var overlay = new OverlayWindow
            {
                Left = rect.Left,
                Top = rect.Top,
                Height = rect.Height,
                Width = rect.Width,
            };

            overlay.Show();
            Thread.Sleep(50);
            overlay.Close();
        }
    }
}