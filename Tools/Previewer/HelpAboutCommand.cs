using Sce.Atf.Controls;
using Sce.Atf;
using System;
using System.Collections.Generic;
using System.ComponentModel.Composition;
using System.Windows.Forms;
using Sce.Atf.Controls.Adaptable;

namespace Previewer
{
    [Export(typeof(HelpAboutCommand))]
    [Export(typeof(IInitializable))]
    [PartCreationPolicy(CreationPolicy.Shared)]
    public class HelpAboutCommand : Sce.Atf.Applications.HelpAboutCommand
    {
        /// <summary>
        /// Shows the About dialog box</summary>
        protected override void ShowHelpAbout()
        {
            string appName = "Xenoviewer (early alpha)";

            var textinfo = new List<string>();
            textinfo.Add("If assets don't look right, it may be because not all features of the original game engine are emulated.");
            textinfo.Add("Drop by our page on nexus mods if you want to log a bug report or make a feature request (thanks!)");
            textinfo.Add("Try meshes/setdressing if you're not sure where to start.");
            textinfo.Add("Enabling 'lighting mode' will eat a lot of GPU power for a few minutes for a compile operation. This is only done once, and the result is cached in your Temp directory.");

            RichTextBox richTextBox = new RichTextBox();
            richTextBox.BorderStyle = BorderStyle.None;
            richTextBox.ReadOnly = true;
            // richTextBox.Text = "Xenoviewer (early alpha)\nGame asset viewer. Dedicated to those that want to appreciate well made assets, and also understand how they are made.\nEarly alpha. Some jankiness expected (nice hack, mate).";
            richTextBox.Rtf = "{\\rtf1\\fs30 Xenoviewer (early alpha)\\fs20\\par\\par Game asset viewer. For those who want to appreciate well made game assets and also understand how they are made.\\par Early alpha. Some jankiness expected \\i (nice hack, mate)\\i0.}";

            System.Drawing.Image image = Previewer.Properties.Resources.xeno_about;

            string appURL = null; //  "[check nexusmods]";
            AboutDialog dialog = new AboutDialog(appName, appURL, richTextBox, image, textinfo, false);
            dialog.ShowDialog();
        }
    }
}
