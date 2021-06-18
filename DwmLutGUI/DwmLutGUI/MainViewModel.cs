using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
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
        private readonly Dictionary<uint, string> _config;

        public MainViewModel()
        {
            UpdateActiveStatus();
            var dispatcherTimer = new System.Windows.Threading.DispatcherTimer();
            dispatcherTimer.Tick += DispatcherTimer_Tick;
            dispatcherTimer.Interval = new TimeSpan(0, 0, 1);
            dispatcherTimer.Start();

            _configPath = AppDomain.CurrentDomain.BaseDirectory + "config.xml";
            if (File.Exists(_configPath))
            {
                var xElem = XElement.Load(_configPath);
                _config = xElem.Descendants("monitor")
                    .ToDictionary(x => (uint) x.Attribute("id"), x => (string) x.Attribute("lut"));
            }
            else
            {
                _config = new Dictionary<uint, string>();
            }

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
                OnPropertyChanged(nameof(LutPath));
            }
            get => _selectedMonitor;
        }

        public string LutPath
        {
            set
            {
                if (SelectedMonitor == null || SelectedMonitor.LutPath == value) return;
                SelectedMonitor.LutPath = value;
                OnPropertyChanged();

                var key = SelectedMonitor.DeviceId;
                if (!string.IsNullOrEmpty(value))
                {
                    _config[key] = value;
                }
                else
                {
                    _config.Remove(key);
                }

                var xElem = new XElement("monitors",
                    _config.Select(x =>
                        new XElement("monitor", new XAttribute("id", x.Key), new XAttribute("lut", x.Value))));
                xElem.Save(_configPath);
            }
            get => SelectedMonitor?.LutPath;
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
                var refreshRate = (targetInfo.FrequencyInMillihertz / 1000.0).ToString("n3") + " Hz";
                var connector = targetInfo.OutputTechnology.ToString();
                if (connector == "DisplayPortExternal")
                {
                    connector = "DisplayPort";
                }

                var position = path.Position.X + "," + path.Position.Y;

                string lutPath = null;
                if (_config.ContainsKey(deviceId))
                {
                    lutPath = _config[deviceId];
                }

                Monitors.Add(new MonitorData(deviceId, path.DisplaySource.SourceId + 1, name, resolution, refreshRate,
                    connector, position, lutPath));
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
            if (!Monitors.All(monitor => string.IsNullOrEmpty(monitor.LutPath)))
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
                IsActive = (bool) status;
                ActiveText = (bool) status ? "Active" : "Inactive";
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