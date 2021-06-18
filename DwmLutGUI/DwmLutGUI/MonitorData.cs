using System.ComponentModel;
using System.IO;

namespace DwmLutGUI
{
    public class MonitorData : INotifyPropertyChanged
    {
        public event PropertyChangedEventHandler PropertyChanged;

        private string _lutPath;

        public MonitorData(uint deviceId, uint sourceId, string name, string resolution, string refreshRate,
            string connector, string position, string lutPath)
        {
            DeviceId = deviceId;
            SourceId = sourceId;
            Name = name;
            Resolution = resolution;
            RefreshRate = refreshRate;
            Connector = connector;
            Position = position;
            LutPath = lutPath;
        }

        public uint DeviceId { get; }
        public uint SourceId { get; }
        public string Name { get; }
        public string Resolution { get; }
        public string RefreshRate { get; }
        public string Connector { get; }
        public string Position { get; }

        public string LutPath
        {
            set
            {
                if (value == _lutPath) return;
                _lutPath = value;
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(LutFilename)));
            }
            get => _lutPath;
        }

        public string LutFilename => Path.GetFileName(LutPath) ?? "None";
    }
}