using System;
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
            InitializeComponent();
            _viewModel = (MainViewModel) DataContext;
            _applyOnCooldown = false;
        }

        private static string BrowseLuts()
        {
            var dlg = new Microsoft.Win32.OpenFileDialog
            {
                Filter = "Cube LUTs|*.cube"
            };

            var result = dlg.ShowDialog();

            return (result == true) ? dlg.FileName : null;
        }

        private void MonitorRefreshButton_Click(object sender, System.Windows.RoutedEventArgs e)
        {
            _viewModel.UpdateMonitors();
        }

        private void LutBrowse_Click(object sender, System.Windows.RoutedEventArgs e)
        {
            var lutPath = BrowseLuts();
            if (!string.IsNullOrEmpty(lutPath))
            {
                _viewModel.LutPath = lutPath;
            }
        }

        private void LutClear_Click(object sender, System.Windows.RoutedEventArgs e)
        {
            _viewModel.LutPath = null;
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