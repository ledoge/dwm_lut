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
                Monitors.Select(x =>
                    new XElement("monitor", new XAttribute("id", x.DeviceId),
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

        public ObservableCollection<MonitorData> Monitors { get; }

        public void UpdateMonitors()
        {
            var selectedId = SelectedMonitor?.DeviceId;
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
                var settings = config?.FirstOrDefault(x => (uint)x.Attribute("id") == deviceId);
                if (settings != null)
                {
                    sdrLutPath = (string)settings.Attribute("sdr_lut");
                    hdrLutPath = (string)settings.Attribute("hdr_lut");
                }

                Monitors.Add(new MonitorData(deviceId, path.DisplaySource.SourceId + 1, name, resolution, refreshRate,
                    connector, position, sdrLutPath, hdrLutPath));
            }

            if (selectedId == null) return;

            var previous = Monitors.FirstOrDefault(monitor => monitor.DeviceId == selectedId);
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