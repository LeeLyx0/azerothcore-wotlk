using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.RegularExpressions;
using System.Windows.Forms;

namespace PlayerbotsConfEditor
{
    public partial class Form1 : Form
    {
        private const string DefaultConfigPath = @"C:\AzerothCore\configs\modules\playerbots.conf";

        private readonly List<ConfigOption> _options = new List<ConfigOption>();
        private readonly Regex _optionRegex = new Regex(@"^\s*(?<name>[A-Za-z0-9_.]+)\s*=\s*(?<value>.*)$", RegexOptions.Compiled);

        private DataGridView _optionsGrid;
        private TextBox _pathTextBox;
        private TextBox _filterTextBox;
        private TextBox _detailsTextBox;
        private Label _statusLabel;
        private Label _countLabel;
        private Button _saveButton;
        private Button _reloadButton;

        private string[] _lines = new string[0];
        private string _loadedPath;
        private string _lineEnding = Environment.NewLine;
        private Encoding _fileEncoding = new UTF8Encoding(false);
        private bool _loadingGrid;

        public Form1()
        {
            InitializeComponent();
            BuildLayout();
            LoadDefaultConfig();
        }

        private void BuildLayout()
        {
            Font = new Font("Segoe UI", 9F, FontStyle.Regular, GraphicsUnit.Point);

            var root = new TableLayoutPanel
            {
                Dock = DockStyle.Fill,
                ColumnCount = 1,
                RowCount = 5,
                Padding = new Padding(12),
            };
            root.RowStyles.Add(new RowStyle(SizeType.Absolute, 36F));
            root.RowStyles.Add(new RowStyle(SizeType.Absolute, 36F));
            root.RowStyles.Add(new RowStyle(SizeType.Percent, 100F));
            root.RowStyles.Add(new RowStyle(SizeType.Absolute, 92F));
            root.RowStyles.Add(new RowStyle(SizeType.Absolute, 38F));
            Controls.Add(root);

            root.Controls.Add(BuildPathPanel(), 0, 0);
            root.Controls.Add(BuildFilterPanel(), 0, 1);
            root.Controls.Add(BuildOptionsGrid(), 0, 2);
            root.Controls.Add(BuildDetailsBox(), 0, 3);
            root.Controls.Add(BuildBottomPanel(), 0, 4);
        }

        private Control BuildPathPanel()
        {
            var panel = new TableLayoutPanel
            {
                Dock = DockStyle.Fill,
                ColumnCount = 4,
                Margin = new Padding(0, 0, 0, 6),
            };
            panel.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 88F));
            panel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
            panel.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 92F));
            panel.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 92F));

            var label = new Label
            {
                Dock = DockStyle.Fill,
                Text = "Config file",
                TextAlign = ContentAlignment.MiddleLeft,
            };

            _pathTextBox = new TextBox
            {
                Dock = DockStyle.Fill,
                Margin = new Padding(0, 6, 8, 0),
            };
            _pathTextBox.KeyDown += PathTextBox_KeyDown;

            var browseButton = new Button
            {
                Dock = DockStyle.Fill,
                Text = "Browse...",
                Margin = new Padding(0, 2, 8, 2),
            };
            browseButton.Click += BrowseButton_Click;

            _reloadButton = new Button
            {
                Dock = DockStyle.Fill,
                Text = "Reload",
                Margin = new Padding(0, 2, 0, 2),
            };
            _reloadButton.Click += ReloadButton_Click;

            panel.Controls.Add(label, 0, 0);
            panel.Controls.Add(_pathTextBox, 1, 0);
            panel.Controls.Add(browseButton, 2, 0);
            panel.Controls.Add(_reloadButton, 3, 0);

            return panel;
        }

        private Control BuildFilterPanel()
        {
            var panel = new TableLayoutPanel
            {
                Dock = DockStyle.Fill,
                ColumnCount = 3,
                Margin = new Padding(0, 0, 0, 6),
            };
            panel.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 88F));
            panel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
            panel.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 190F));

            var label = new Label
            {
                Dock = DockStyle.Fill,
                Text = "Filter",
                TextAlign = ContentAlignment.MiddleLeft,
            };

            _filterTextBox = new TextBox
            {
                Dock = DockStyle.Fill,
                Margin = new Padding(0, 6, 8, 0),
            };
            _filterTextBox.TextChanged += FilterTextBox_TextChanged;

            _countLabel = new Label
            {
                Dock = DockStyle.Fill,
                TextAlign = ContentAlignment.MiddleRight,
            };

            panel.Controls.Add(label, 0, 0);
            panel.Controls.Add(_filterTextBox, 1, 0);
            panel.Controls.Add(_countLabel, 2, 0);

            return panel;
        }

        private Control BuildOptionsGrid()
        {
            _optionsGrid = new DataGridView
            {
                AllowUserToAddRows = false,
                AllowUserToDeleteRows = false,
                AutoSizeRowsMode = DataGridViewAutoSizeRowsMode.None,
                BackgroundColor = SystemColors.Window,
                BorderStyle = BorderStyle.FixedSingle,
                ColumnHeadersHeightSizeMode = DataGridViewColumnHeadersHeightSizeMode.AutoSize,
                Dock = DockStyle.Fill,
                EditMode = DataGridViewEditMode.EditOnEnter,
                MultiSelect = false,
                RowHeadersVisible = false,
                SelectionMode = DataGridViewSelectionMode.FullRowSelect,
            };
            _optionsGrid.Columns.Add(new DataGridViewTextBoxColumn
            {
                HeaderText = "Section",
                Name = "SectionColumn",
                ReadOnly = true,
                Width = 170,
            });
            _optionsGrid.Columns.Add(new DataGridViewTextBoxColumn
            {
                HeaderText = "Option",
                Name = "NameColumn",
                ReadOnly = true,
                Width = 280,
            });
            _optionsGrid.Columns.Add(new DataGridViewTextBoxColumn
            {
                HeaderText = "Value",
                Name = "ValueColumn",
                AutoSizeMode = DataGridViewAutoSizeColumnMode.Fill,
                MinimumWidth = 220,
            });
            _optionsGrid.Columns.Add(new DataGridViewTextBoxColumn
            {
                HeaderText = "Default",
                Name = "DefaultColumn",
                ReadOnly = true,
                Width = 110,
            });
            _optionsGrid.Columns.Add(new DataGridViewTextBoxColumn
            {
                HeaderText = "Line",
                Name = "LineColumn",
                ReadOnly = true,
                Width = 64,
            });

            _optionsGrid.CellValueChanged += OptionsGrid_CellValueChanged;
            _optionsGrid.CurrentCellDirtyStateChanged += OptionsGrid_CurrentCellDirtyStateChanged;
            _optionsGrid.DataError += OptionsGrid_DataError;
            _optionsGrid.SelectionChanged += OptionsGrid_SelectionChanged;

            return _optionsGrid;
        }

        private Control BuildDetailsBox()
        {
            _detailsTextBox = new TextBox
            {
                BackColor = SystemColors.ControlLightLight,
                Dock = DockStyle.Fill,
                Multiline = true,
                ReadOnly = true,
                ScrollBars = ScrollBars.Vertical,
            };

            return _detailsTextBox;
        }

        private Control BuildBottomPanel()
        {
            var panel = new TableLayoutPanel
            {
                Dock = DockStyle.Fill,
                ColumnCount = 2,
                Margin = new Padding(0, 8, 0, 0),
            };
            panel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
            panel.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 120F));

            _statusLabel = new Label
            {
                Dock = DockStyle.Fill,
                TextAlign = ContentAlignment.MiddleLeft,
            };

            _saveButton = new Button
            {
                Dock = DockStyle.Fill,
                Enabled = false,
                Text = "Save",
            };
            _saveButton.Click += SaveButton_Click;

            panel.Controls.Add(_statusLabel, 0, 0);
            panel.Controls.Add(_saveButton, 1, 0);

            return panel;
        }

        private void LoadDefaultConfig()
        {
            _pathTextBox.Text = DefaultConfigPath;

            if (File.Exists(DefaultConfigPath))
            {
                LoadConfig(DefaultConfigPath);
                return;
            }

            SetStatus("Default playerbots.conf was not found. Browse to the file to load options.");
        }

        private void LoadConfig(string filePath)
        {
            try
            {
                if (!File.Exists(filePath))
                {
                    ClearOptions();
                    SetStatus("File not found. Browse to playerbots.conf or enter a valid path.");
                    return;
                }

                _fileEncoding = DetectEncoding(filePath);
                string fileText = File.ReadAllText(filePath, _fileEncoding);
                _lineEnding = DetectLineEnding(fileText);
                _lines = Regex.Split(fileText, "\r\n|\n|\r");
                _loadedPath = filePath;

                ParseOptions();
                PopulateGrid();
                ApplyFilter();
                SetStatus("Loaded " + _options.Count + " editable options from " + filePath);
            }
            catch (Exception ex)
            {
                ClearOptions();
                SetStatus("Unable to load config: " + ex.Message);
            }
        }

        private void ParseOptions()
        {
            _options.Clear();

            string currentSection = "General";
            var pendingComments = new List<string>();

            for (int i = 0; i < _lines.Length; ++i)
            {
                string line = _lines[i];
                string trimmed = line.Trim();

                if (trimmed.StartsWith("#", StringComparison.Ordinal))
                {
                    string comment = CleanComment(trimmed);

                    if (IsSectionHeading(comment))
                        currentSection = ToDisplaySection(comment);

                    pendingComments.Add(comment);
                    continue;
                }

                if (string.IsNullOrWhiteSpace(trimmed))
                {
                    pendingComments.Clear();
                    continue;
                }

                Match match = _optionRegex.Match(line);

                if (match.Success)
                {
                    string name = match.Groups["name"].Value.Trim();
                    string value = match.Groups["value"].Value.Trim();
                    string description = BuildDescription(pendingComments);

                    var option = new ConfigOption
                    {
                        LineIndex = i,
                        Name = name,
                        OriginalValue = value,
                        Section = currentSection,
                        Description = description,
                    };
                    option.Choices = BuildChoices(option);
                    option.DefaultValue = ExtractDefaultValue(description);

                    _options.Add(option);
                }

                pendingComments.Clear();
            }
        }

        private void PopulateGrid()
        {
            _loadingGrid = true;
            _optionsGrid.Rows.Clear();

            foreach (ConfigOption option in _options)
            {
                int rowIndex = _optionsGrid.Rows.Add(
                    option.Section,
                    option.Name,
                    option.OriginalValue,
                    option.DefaultValue,
                    option.LineIndex + 1);

                DataGridViewRow row = _optionsGrid.Rows[rowIndex];
                row.Tag = option;

                if (option.Choices.Count > 1)
                {
                    int valueColumnIndex = _optionsGrid.Columns["ValueColumn"].Index;
                    var comboCell = new DataGridViewComboBoxCell
                    {
                        DisplayMember = "Display",
                        ValueMember = "Value",
                    };

                    foreach (ConfigChoice choice in option.Choices)
                        comboCell.Items.Add(choice);

                    comboCell.Value = option.OriginalValue;
                    row.Cells[valueColumnIndex] = comboCell;
                }
            }

            _loadingGrid = false;
            UpdateDetails();
            UpdateSaveButtonState();
        }

        private List<ConfigChoice> BuildChoices(ConfigOption option)
        {
            if (IsFreeNumericOption(option))
                return new List<ConfigChoice>();

            var choices = ExtractExplicitChoices(option.Description);

            if (IsYesNoValue(option.OriginalValue))
            {
                AddChoice(choices, "yes", "Enabled");
                AddChoice(choices, "no", "Disabled");
            }

            if (IsBooleanOption(option))
            {
                AddChoice(choices, "0", "Disabled");
                AddChoice(choices, "1", "Enabled");
            }

            if (choices.Count == 0)
                return new List<ConfigChoice>();

            AddChoice(choices, option.OriginalValue, "Current value");

            return choices
                .OrderBy(c => ChoiceSortKey(c.Value))
                .ThenBy(c => c.Value, StringComparer.OrdinalIgnoreCase)
                .ToList();
        }

        private static bool IsFreeNumericOption(ConfigOption option)
        {
            return string.Equals(option.Name, "AiPlayerbot.BotActiveAlone", StringComparison.Ordinal);
        }

        private List<ConfigChoice> ExtractExplicitChoices(string description)
        {
            var choices = new List<ConfigChoice>();

            if (string.IsNullOrWhiteSpace(description))
                return choices;

            MatchCollection matches = Regex.Matches(
                description,
                @"(?<![\w.])(?<value>-?\d+(?:\.\d+)?)\s*(?:=|:|-)\s*(?<label>[^,;|)]+)",
                RegexOptions.IgnoreCase);

            foreach (Match match in matches)
            {
                string value = match.Groups["value"].Value.Trim();
                string label = CleanChoiceLabel(match.Groups["label"].Value);

                if (string.IsNullOrWhiteSpace(label) || !label.Any(char.IsLetter))
                    continue;

                AddChoice(choices, value, label);
            }

            if (choices.Count > 20)
                choices.Clear();

            return choices;
        }

        private static string CleanChoiceLabel(string label)
        {
            string cleaned = Regex.Replace(label.Trim(), @"\s+", " ");
            cleaned = cleaned.Trim('.', ':', '-', ' ');

            return cleaned;
        }

        private static void AddChoice(List<ConfigChoice> choices, string value, string label)
        {
            if (string.IsNullOrWhiteSpace(value))
                return;

            ConfigChoice existing = choices.FirstOrDefault(c => string.Equals(c.Value, value, StringComparison.OrdinalIgnoreCase));

            if (existing != null)
                return;

            choices.Add(new ConfigChoice(value, label));
        }

        private static bool IsBooleanOption(ConfigOption option)
        {
            if (option.OriginalValue != "0" && option.OriginalValue != "1")
                return false;

            string probe = (option.Name + " " + option.Description).ToLowerInvariant();

            return probe.Contains("enable")
                || probe.Contains("disable")
                || probe.Contains("allow")
                || probe.Contains("enabled")
                || probe.Contains("disabled")
                || probe.Contains("toggle")
                || probe.Contains("permission");
        }

        private static bool IsYesNoValue(string value)
        {
            return string.Equals(value, "yes", StringComparison.OrdinalIgnoreCase)
                || string.Equals(value, "no", StringComparison.OrdinalIgnoreCase);
        }

        private static double ChoiceSortKey(string value)
        {
            double number;

            if (double.TryParse(value, out number))
                return number;

            if (string.Equals(value, "no", StringComparison.OrdinalIgnoreCase))
                return 0;

            if (string.Equals(value, "yes", StringComparison.OrdinalIgnoreCase))
                return 1;

            return double.MaxValue;
        }

        private static string ExtractDefaultValue(string description)
        {
            if (string.IsNullOrWhiteSpace(description))
                return string.Empty;

            Match match = Regex.Match(description, @"Default\s*:?\s*(?<value>""[^""]*""|[^\s(,|]+)", RegexOptions.IgnoreCase);

            if (!match.Success)
                return string.Empty;

            return match.Groups["value"].Value.Trim();
        }

        private static string BuildDescription(List<string> comments)
        {
            var meaningfulComments = comments
                .Select(CleanComment)
                .Where(c => !string.IsNullOrWhiteSpace(c))
                .Where(c => !IsCommentNoise(c))
                .Where(c => !IsSectionHeading(c))
                .ToList();

            if (meaningfulComments.Count > 10)
                meaningfulComments = meaningfulComments.Skip(meaningfulComments.Count - 10).ToList();

            return string.Join(" | ", meaningfulComments);
        }

        private static string CleanComment(string comment)
        {
            string cleaned = comment.Trim();

            while (cleaned.StartsWith("#", StringComparison.Ordinal))
                cleaned = cleaned.Substring(1).Trim();

            return Regex.Replace(cleaned, @"\s+", " ").Trim();
        }

        private static bool IsCommentNoise(string comment)
        {
            if (string.IsNullOrWhiteSpace(comment))
                return true;

            return comment.All(c => c == '#' || c == '-' || c == '=' || c == '*');
        }

        private static bool IsSectionHeading(string comment)
        {
            if (string.IsNullOrWhiteSpace(comment))
                return false;

            if (comment.Length > 64 || comment.Contains("=") || comment.Contains(".") || comment.Contains(":"))
                return false;

            if (!comment.Any(char.IsLetter))
                return false;

            string normalized = comment.Replace("&", string.Empty).Replace("-", string.Empty).Trim();

            return normalized.Length > 2 && normalized == normalized.ToUpperInvariant();
        }

        private static string ToDisplaySection(string section)
        {
            return Regex.Replace(section.Trim(), @"\s+", " ");
        }

        private void SaveButton_Click(object sender, EventArgs e)
        {
            if (string.IsNullOrWhiteSpace(_loadedPath))
            {
                SetStatus("No config file is loaded.");
                return;
            }

            try
            {
                _optionsGrid.EndEdit();

                int changedCount = ApplyGridValuesToLines();

                if (changedCount == 0)
                {
                    SetStatus("No changes to save.");
                    UpdateSaveButtonState();
                    return;
                }

                File.WriteAllText(_loadedPath, string.Join(_lineEnding, _lines), _fileEncoding);

                foreach (ConfigOption option in _options)
                    option.OriginalValue = option.CurrentValue;

                PopulateGrid();
                ApplyFilter();
                SetStatus("Saved " + changedCount + " change" + (changedCount == 1 ? string.Empty : "s") + " to " + _loadedPath);
            }
            catch (Exception ex)
            {
                SetStatus("Unable to save config: " + ex.Message);
                MessageBox.Show(this, ex.Message, "Save failed", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }

        private int ApplyGridValuesToLines()
        {
            int changedCount = 0;

            foreach (DataGridViewRow row in _optionsGrid.Rows)
            {
                var option = row.Tag as ConfigOption;

                if (option == null)
                    continue;

                string newValue = GetCellRawValue(row.Cells["ValueColumn"]);
                option.CurrentValue = newValue;

                if (string.Equals(option.OriginalValue, newValue, StringComparison.Ordinal))
                    continue;

                _lines[option.LineIndex] = ReplaceOptionValue(_lines[option.LineIndex], newValue);
                ++changedCount;
            }

            return changedCount;
        }

        private static string GetCellRawValue(DataGridViewCell cell)
        {
            if (cell.Value == null)
                return string.Empty;

            var choice = cell.Value as ConfigChoice;

            if (choice != null)
                return choice.Value;

            return Convert.ToString(cell.Value).Trim();
        }

        private static string ReplaceOptionValue(string line, string newValue)
        {
            int equalsIndex = line.IndexOf('=');

            if (equalsIndex < 0)
                return line;

            return line.Substring(0, equalsIndex + 1) + " " + newValue;
        }

        private void BrowseButton_Click(object sender, EventArgs e)
        {
            using (var dialog = new OpenFileDialog())
            {
                dialog.Title = "Select playerbots.conf";
                dialog.Filter = "playerbots.conf|playerbots.conf|Config files (*.conf)|*.conf|All files (*.*)|*.*";
                dialog.FileName = "playerbots.conf";

                string currentPath = _pathTextBox.Text.Trim();
                string initialDirectory = GetExistingDirectory(currentPath) ?? GetExistingDirectory(DefaultConfigPath);

                if (!string.IsNullOrWhiteSpace(initialDirectory))
                    dialog.InitialDirectory = initialDirectory;

                if (dialog.ShowDialog(this) != DialogResult.OK)
                    return;

                _pathTextBox.Text = dialog.FileName;
                LoadConfig(dialog.FileName);
            }
        }

        private static string GetExistingDirectory(string path)
        {
            try
            {
                if (string.IsNullOrWhiteSpace(path))
                    return null;

                if (Directory.Exists(path))
                    return path;

                string directory = Path.GetDirectoryName(path);

                if (Directory.Exists(directory))
                    return directory;
            }
            catch
            {
                return null;
            }

            return null;
        }

        private void ReloadButton_Click(object sender, EventArgs e)
        {
            LoadConfig(_pathTextBox.Text.Trim());
        }

        private void PathTextBox_KeyDown(object sender, KeyEventArgs e)
        {
            if (e.KeyCode != Keys.Enter)
                return;

            e.SuppressKeyPress = true;
            LoadConfig(_pathTextBox.Text.Trim());
        }

        private void FilterTextBox_TextChanged(object sender, EventArgs e)
        {
            ApplyFilter();
        }

        private void ApplyFilter()
        {
            if (_optionsGrid == null)
                return;

            string filter = _filterTextBox.Text.Trim();
            int visibleCount = 0;
            _optionsGrid.CurrentCell = null;

            foreach (DataGridViewRow row in _optionsGrid.Rows)
            {
                var option = row.Tag as ConfigOption;

                if (option == null)
                    continue;

                bool visible = string.IsNullOrWhiteSpace(filter)
                    || option.Name.IndexOf(filter, StringComparison.OrdinalIgnoreCase) >= 0
                    || option.Section.IndexOf(filter, StringComparison.OrdinalIgnoreCase) >= 0
                    || option.Description.IndexOf(filter, StringComparison.OrdinalIgnoreCase) >= 0
                    || GetCellRawValue(row.Cells["ValueColumn"]).IndexOf(filter, StringComparison.OrdinalIgnoreCase) >= 0;

                row.Visible = visible;

                if (visible)
                    ++visibleCount;
            }

            _countLabel.Text = visibleCount + " of " + _options.Count + " options";
        }

        private void OptionsGrid_CellValueChanged(object sender, DataGridViewCellEventArgs e)
        {
            if (_loadingGrid || e.RowIndex < 0)
                return;

            UpdateSaveButtonState();
        }

        private void OptionsGrid_CurrentCellDirtyStateChanged(object sender, EventArgs e)
        {
            if (_optionsGrid.IsCurrentCellDirty)
                _optionsGrid.CommitEdit(DataGridViewDataErrorContexts.Commit);
        }

        private void OptionsGrid_DataError(object sender, DataGridViewDataErrorEventArgs e)
        {
            e.ThrowException = false;
        }

        private void OptionsGrid_SelectionChanged(object sender, EventArgs e)
        {
            UpdateDetails();
        }

        private void UpdateDetails()
        {
            if (_optionsGrid.CurrentRow == null)
            {
                _detailsTextBox.Text = string.Empty;
                return;
            }

            var option = _optionsGrid.CurrentRow.Tag as ConfigOption;

            if (option == null)
            {
                _detailsTextBox.Text = string.Empty;
                return;
            }

            var details = new StringBuilder();
            details.AppendLine(option.Name);
            details.AppendLine("Current value: " + GetCellRawValue(_optionsGrid.CurrentRow.Cells["ValueColumn"]));

            if (!string.IsNullOrWhiteSpace(option.DefaultValue))
                details.AppendLine("Default: " + option.DefaultValue);

            if (!string.IsNullOrWhiteSpace(option.Description))
            {
                details.AppendLine();
                details.AppendLine(option.Description.Replace(" | ", Environment.NewLine));
            }

            _detailsTextBox.Text = details.ToString();
        }

        private void UpdateSaveButtonState()
        {
            if (_saveButton == null)
                return;

            bool hasChanges = false;

            foreach (DataGridViewRow row in _optionsGrid.Rows)
            {
                var option = row.Tag as ConfigOption;

                if (option == null)
                    continue;

                string value = GetCellRawValue(row.Cells["ValueColumn"]);

                if (!string.Equals(option.OriginalValue, value, StringComparison.Ordinal))
                {
                    hasChanges = true;
                    break;
                }
            }

            _saveButton.Enabled = hasChanges && !string.IsNullOrWhiteSpace(_loadedPath);
        }

        private void ClearOptions()
        {
            _options.Clear();
            _lines = new string[0];
            _loadedPath = null;
            _optionsGrid.Rows.Clear();
            _detailsTextBox.Text = string.Empty;
            _countLabel.Text = "0 of 0 options";
            _saveButton.Enabled = false;
        }

        private void SetStatus(string message)
        {
            _statusLabel.Text = message;
        }

        private static string DetectLineEnding(string text)
        {
            if (text.Contains("\r\n"))
                return "\r\n";

            if (text.Contains("\n"))
                return "\n";

            if (text.Contains("\r"))
                return "\r";

            return Environment.NewLine;
        }

        private static Encoding DetectEncoding(string filePath)
        {
            var bom = new byte[4];

            using (FileStream stream = File.OpenRead(filePath))
                stream.Read(bom, 0, bom.Length);

            if (bom[0] == 0xEF && bom[1] == 0xBB && bom[2] == 0xBF)
                return new UTF8Encoding(true);

            if (bom[0] == 0xFF && bom[1] == 0xFE)
                return Encoding.Unicode;

            if (bom[0] == 0xFE && bom[1] == 0xFF)
                return Encoding.BigEndianUnicode;

            return new UTF8Encoding(false);
        }

        private sealed class ConfigOption
        {
            public int LineIndex { get; set; }
            public string Name { get; set; }
            public string Section { get; set; }
            public string OriginalValue { get; set; }
            public string CurrentValue { get; set; }
            public string DefaultValue { get; set; }
            public string Description { get; set; }
            public List<ConfigChoice> Choices { get; set; }
        }

        private sealed class ConfigChoice
        {
            public ConfigChoice(string value, string label)
            {
                Value = value;
                Label = label;
            }

            public string Value { get; private set; }
            public string Label { get; private set; }

            public string Display
            {
                get
                {
                    if (string.IsNullOrWhiteSpace(Label))
                        return Value;

                    return Value + " - " + Label;
                }
            }

            public override string ToString()
            {
                return Display;
            }
        }
    }
}
