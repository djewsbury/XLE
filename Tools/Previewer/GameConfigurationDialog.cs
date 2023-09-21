using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace Previewer
{
    public partial class GameConfigurationDialog : Form
    {
        public GameConfigurationDialog()
        {
            InitializeComponent();

            // We can't get this from the registry, so just pick a common default
            starfieldFolder.Text = "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Starfield";
        }

        private void starfieldSelectFolder_Click(object sender, EventArgs e)
        {
            using (var fbd = new FolderBrowserDialog())
            {
                fbd.SelectedPath = starfieldFolder.Text;
                DialogResult result = fbd.ShowDialog();

                if (result == DialogResult.OK && !string.IsNullOrWhiteSpace(fbd.SelectedPath))
                {
                    starfieldFolder.Text = fbd.SelectedPath;
                }
            }
        }

        private void fakeOkButton_Click(object sender, EventArgs e)
        {
            var starfieldFolderValue = starfieldFolder.Text.Trim();
            starfieldFolder.Enabled = false;

            // Use XLE bridge utils to configure 
            fakeOkButton.Enabled = false;
            fakeOkButton.Visible = false;
            progressListBox.Enabled = true;
            progressListBox.Visible = true;
            progressListBox.Items.Add("Please wait while loading game plugins...");

            using (var opContext = new GUILayer.OperationContextWrapper())
            {
                var marker = GUILayer.Utils.BeginPluginConfigure(
                    opContext, "Starfield",
                    new Dictionary<string, string>() {
                        {  "SrcFolder", starfieldFolderValue }
                    });

                while (!marker.Poll(100))
                {
                    var ops = opContext.GetActiveOperations();
                    progressListBox.BeginUpdate();
                    progressListBox.Items.Clear();
                    foreach (var op in ops)
                    {
                        if (!string.IsNullOrEmpty(op._description))
                            progressListBox.Items.Add(op._description);
                        if (!string.IsNullOrEmpty(op._msg))
                            progressListBox.Items.Add(op._msg);
                        if (op._progressMax != 0)
                            progressListBox.Items.Add(op._progress.ToString() + " / " + op._progressMax.ToString());
                    }
                    progressListBox.EndUpdate();
                    Refresh();
                }

                // change mode once again, to show final results
                progressListBox.Enabled = false;
                progressListBox.Visible = false;
                string log = marker.GetActualizationLog();
                if (!string.IsNullOrEmpty(log))
                {
                    applyResults.Text = log.Replace("\n", Environment.NewLine);
                }
                else
                    applyResults.Text = "No logging information (probably because no plugins were applied).";
                applyResults.Visible = true;
                applyResults.Enabled = true;
                fakeOkButton.Enabled = false;
                fakeOkButton.Visible = false;
                okButton.Enabled = true;
                okButton.Visible = true;

                marker.Dispose();
            }
        }
    }
}
