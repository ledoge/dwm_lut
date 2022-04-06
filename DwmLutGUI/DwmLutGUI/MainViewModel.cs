using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Xml.Linq;

namespace DwmLutGUI
{
    internal class MainViewModel : INotifyPropertyChanged
    {
        public event PropertyChangedEventHandler PropertyChanged;

        private string _activeText;
        private MonitorData _selectedMonitor;
        private bool _isActive;

        private readonly string _configPath;

        public MainViewModel()
        {
            UpdateActiveStatus();
            var dispatcherTimer = new System.Windows.Threading.DispatcherTimer();
            dispatcherTimer.Tick += DispatcherTimer_Tick;
            dispatcherTimer.Interval = new TimeSpan(0, 0, 1);
            dispatcherTimer.Start();

            _configPath = AppDomain.CurrentDomain.BaseDirectory + "config.xml";

            _allMonitors = new List<MonitorData>();
            Monitors = new ObservableCollection<MonitorData>();
            UpdateMonitors();

            CanApply = !Injector.NoDebug;
        }

        public string ActiveText
        {
            private set
            {
                if (value == _activeText) return;
                _activeText = value;
                OnPropertyChanged();
            }
            get => _activeText;
        }

        public MonitorData SelectedMonitor
        {
            set
            {
                if (value == _selectedMonitor) return;
                _selectedMonitor = value;
                OnPropertyChanged();
                OnPropertyChanged(nameof(SdrLutPath));
                OnPropertyChanged(nameof(HdrLutPath));
            }
            get => _selectedMonitor;
        }

        private void SaveConfig()
        {
            var xElem = new XElement("monitors",
                _allMonitors.Select(x =>
                    new XElement("monitor", new XAttribute("path", x.DevicePath),
                        x.SdrLutPath != null ? new XAttribute("sdr_lut", x.SdrLutPath) : null,
                        x.HdrLutPath != null ? new XAttribute("hdr_lut", x.HdrLutPath) : null)));
            xElem.Save(_configPath);
        }

        public string SdrLutPath
        {
            set
            {
                if (SelectedMonitor == null || SelectedMonitor.SdrLutPath == value) return;
                SelectedMonitor.SdrLutPath = value;
                OnPropertyChanged();

                SaveConfig();
            }
            get => SelectedMonitor?.SdrLutPath;
        }

        public string HdrLutPath
        {
            set
            {
                if (SelectedMonitor == null || SelectedMonitor.HdrLutPath == value) return;
                SelectedMonitor.HdrLutPath = value;
                OnPropertyChanged();

                SaveConfig();
            }
            get => SelectedMonitor?.HdrLutPath;
        }

        public bool IsActive
        {
            set
            {
                if (value == _isActive) return;
                _isActive = value;
                OnPropertyChanged();
            }
            get => _isActive;
        }

        public bool CanApply { get; }

        private List<MonitorData> _allMonitors { get; }
        public ObservableCollection<MonitorData> Monitors { get; }

        public void UpdateMonitors()
        {
            var selectedPath = SelectedMonitor?.DevicePath;
            Monitors.Clear();
            List<XElement> config = null;
            if (File.Exists(_configPath))
            {
                config = XElement.Load(_configPath).Descendants("monitor").ToList();
            }

            var paths = WindowsDisplayAPI.DisplayConfig.PathInfo.GetActivePaths();
            foreach (var path in paths)
            {
                if (path.IsCloneMember) continue;
                var targetInfo = path.TargetsInfo[0];
                var deviceId = targetInfo.DisplayTarget.TargetId;
                var devicePath = targetInfo.DisplayTarget.DevicePath;
                var name = targetInfo.DisplayTarget.FriendlyName;
                if (string.IsNullOrEmpty(name))
                {
                    name = "???";
                }

                var resolution = path.Resolution.Width + "x" + path.Resolution.Height;
                var refreshRate =
                    (targetInfo.FrequencyInMillihertz / 1000.0).ToString("n3", CultureInfo.InvariantCulture) + " Hz";
                var connector = targetInfo.OutputTechnology.ToString();
                if (connector == "DisplayPortExternal")
                {
                    connector = "DisplayPort";
                }

                var position = path.Position.X + "," + path.Position.Y;

                string sdrLutPath = null;
                string hdrLutPath = null;

                var settings = config?.FirstOrDefault(x => (uint?)x.Attribute("id") == deviceId) ??
                               config?.FirstOrDefault(x => (string)x.Attribute("path") == devicePath);

                if (settings != null)
                {
                    sdrLutPath = (string)settings.Attribute("sdr_lut");
                    hdrLutPath = (string)settings.Attribute("hdr_lut");
                }

                var monitor = new MonitorData(devicePath, path.DisplaySource.SourceId + 1, name, resolution,
                    refreshRate,
                    connector, position, sdrLutPath, hdrLutPath);
                _allMonitors.Add(monitor);
                Monitors.Add(monitor);
            }

            if (config != null)
            {
                foreach (var monitor in config)
                {
                    var path = (string)monitor.Attribute("path");
                    if (path == null || Monitors.Any(x => x.DevicePath == path)) continue;

                    var sdrLutPath = (string)monitor.Attribute("sdr_lut");
                    var hdrLutPath = (string)monitor.Attribute("hdr_lut");

                    _allMonitors.Add(new MonitorData(path, sdrLutPath, hdrLutPath));
                }
            }

            if (selectedPath == null) return;

            var previous = Monitors.FirstOrDefault(monitor => monitor.DevicePath == selectedPath);
            if (previous != null)
            {
                SelectedMonitor = previous;
            }
        }

        public void ReInject()
        {
            Injector.Uninject();
            if (!Monitors.All(monitor =>
                    string.IsNullOrEmpty(monitor.SdrLutPath) && string.IsNullOrEmpty(monitor.HdrLutPath)))
            {
                Injector.Inject(Monitors);
            }

            UpdateActiveStatus();
        }

        public void Uninject()
        {
            Injector.Uninject();
            UpdateActiveStatus();
        }

        private void UpdateActiveStatus()
        {
            var status = Injector.GetStatus();
            if (status != null)
            {
                IsActive = (bool)status;
                ActiveText = (bool)status ? "Active" : "Inactive";
            }
            else
            {
                IsActive = false;
                ActiveText = "???";
            }
        }

        private void OnPropertyChanged([CallerMemberName] string name = null)
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
        }

        private void DispatcherTimer_Tick(object sender, EventArgs e)
        {
            UpdateActiveStatus();
        }
    }
}