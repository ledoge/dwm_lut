using System;
using System.Diagnostics;
using System.Linq;
using System.Reflection;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace DwmLutGUI
{
    public partial class MainWindow
    {
        private readonly MainViewModel _viewModel;
        private bool _applyOnCooldown;

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

            if (args.Contains("-apply"))
            {
                Apply_Click(null, null);
            }
            else if (args.Contains("-disable"))
            {
                Disable_Click(null, null);
            }

            if (args.Contains("-minimize"))
            {
                WindowState = System.Windows.WindowState.Minimized;
                Hide();
            }
            else if (args.Contains("-exit"))
            {
                Close();
                return;
            }

            var notifyIcon = new NotifyIcon();
            var stream = Assembly.GetExecutingAssembly().GetManifestResourceStream("DwmLutGUI.smile.ico");
            notifyIcon.Icon = new System.Drawing.Icon(stream);
            notifyIcon.Visible = true;
            notifyIcon.DoubleClick +=
                delegate
                {
                    Show();
                    WindowState = System.Windows.WindowState.Normal;
                };

            var contextMenu = new ContextMenu();
            var menuItem = new MenuItem();
            contextMenu.MenuItems.Add(menuItem);

            menuItem.Index = 0;
            menuItem.Text = "Exit";
            menuItem.Click += delegate { Close(); };

            notifyIcon.ContextMenu = contextMenu;

            notifyIcon.Text = Assembly.GetEntryAssembly().GetName().Name;

            Closed += delegate { notifyIcon.Dispose(); };
        }

        protected override void OnStateChanged(EventArgs e)
        {
            if (WindowState == System.Windows.WindowState.Minimized)
            {
                Hide();
            }

            base.OnStateChanged(e);
        }

        private static string BrowseLuts()
        {
            var dlg = new Microsoft.Win32.OpenFileDialog
            {
                Filter = "Cube LUTs|*.cube"
            };

            var result = dlg.ShowDialog();

            return result == true ? dlg.FileName : null;
        }

        private void MonitorRefreshButton_Click(object sender, System.Windows.RoutedEventArgs e)
        {
            _viewModel.UpdateMonitors();
        }

        private void SdrLutBrowse_Click(object sender, System.Windows.RoutedEventArgs e)
        {
            var lutPath = BrowseLuts();
            if (!string.IsNullOrEmpty(lutPath))
            {
                _viewModel.SdrLutPath = lutPath;
            }
        }

        private void SdrLutClear_Click(object sender, System.Windows.RoutedEventArgs e)
        {
            _viewModel.SdrLutPath = null;
        }

        private void HdrLutBrowse_Click(object sender, System.Windows.RoutedEventArgs e)
        {
            var lutPath = BrowseLuts();
            if (!string.IsNullOrEmpty(lutPath))
            {
                _viewModel.HdrLutPath = lutPath;
            }
        }

        private void HdrLutClear_Click(object sender, System.Windows.RoutedEventArgs e)
        {
            _viewModel.HdrLutPath = null;
        }

        private void Disable_Click(object sender, System.Windows.RoutedEventArgs e)
        {
            try
            {
                _viewModel.Uninject();
            }
            catch (Exception x)
            {
                MessageBox.Show(x.Message);
            }
        }

        private void Apply_Click(object sender, System.Windows.RoutedEventArgs e)
        {
            if (_applyOnCooldown) return;
            _applyOnCooldown = true;

            try
            {
                _viewModel.ReInject();
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
    }
}