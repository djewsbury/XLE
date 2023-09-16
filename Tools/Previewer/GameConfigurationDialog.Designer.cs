namespace Previewer
{
    partial class GameConfigurationDialog
    {
        /// <summary>
        /// Required designer variable.
        /// </summary>
        private System.ComponentModel.IContainer components = null;

        /// <summary>
        /// Clean up any resources being used.
        /// </summary>
        /// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
        protected override void Dispose(bool disposing)
        {
            if (disposing && (components != null))
            {
                components.Dispose();
            }
            base.Dispose(disposing);
        }

        #region Windows Form Designer generated code

        /// <summary>
        /// Required method for Designer support - do not modify
        /// the contents of this method with the code editor.
        /// </summary>
        private void InitializeComponent()
        {
            System.ComponentModel.ComponentResourceManager resources = new System.ComponentModel.ComponentResourceManager(typeof(GameConfigurationDialog));
            this.starfieldBox1 = new System.Windows.Forms.GroupBox();
            this.label1 = new System.Windows.Forms.Label();
            this.starfieldFolder = new System.Windows.Forms.TextBox();
            this.starfieldSelectFolder = new System.Windows.Forms.Button();
            this.label2 = new System.Windows.Forms.Label();
            this.progressListBox = new System.Windows.Forms.ListBox();
            this.okButton = new System.Windows.Forms.Button();
            this.starfieldBox1.SuspendLayout();
            this.SuspendLayout();
            // 
            // starfieldBox1
            // 
            this.starfieldBox1.Controls.Add(this.label2);
            this.starfieldBox1.Controls.Add(this.starfieldSelectFolder);
            this.starfieldBox1.Controls.Add(this.starfieldFolder);
            this.starfieldBox1.Controls.Add(this.label1);
            resources.ApplyResources(this.starfieldBox1, "starfieldBox1");
            this.starfieldBox1.Name = "starfieldBox1";
            this.starfieldBox1.TabStop = false;
            // 
            // label1
            // 
            resources.ApplyResources(this.label1, "label1");
            this.label1.Name = "label1";
            // 
            // starfieldFolder
            // 
            resources.ApplyResources(this.starfieldFolder, "starfieldFolder");
            this.starfieldFolder.Name = "starfieldFolder";
            // 
            // starfieldSelectFolder
            // 
            resources.ApplyResources(this.starfieldSelectFolder, "starfieldSelectFolder");
            this.starfieldSelectFolder.Name = "starfieldSelectFolder";
            this.starfieldSelectFolder.UseVisualStyleBackColor = true;
            this.starfieldSelectFolder.Click += new System.EventHandler(this.starfieldSelectFolder_Click);
            // 
            // label2
            // 
            resources.ApplyResources(this.label2, "label2");
            this.label2.Name = "label2";
            // 
            // progressListBox
            // 
            resources.ApplyResources(this.progressListBox, "progressListBox");
            this.progressListBox.FormattingEnabled = true;
            this.progressListBox.Name = "progressListBox";
            // 
            // okButton
            // 
            this.okButton.DialogResult = System.Windows.Forms.DialogResult.OK;
            resources.ApplyResources(this.okButton, "okButton");
            this.okButton.Name = "okButton";
            this.okButton.UseVisualStyleBackColor = true;
            this.okButton.Click += new System.EventHandler(this.okButton_Click);
            // 
            // GameConfigurationDialog
            // 
            this.AcceptButton = this.okButton;
            resources.ApplyResources(this, "$this");
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.ControlBox = false;
            this.Controls.Add(this.okButton);
            this.Controls.Add(this.progressListBox);
            this.Controls.Add(this.starfieldBox1);
            this.FormBorderStyle = System.Windows.Forms.FormBorderStyle.FixedDialog;
            this.Name = "GameConfigurationDialog";
            this.starfieldBox1.ResumeLayout(false);
            this.starfieldBox1.PerformLayout();
            this.ResumeLayout(false);

        }

        #endregion

        private System.Windows.Forms.GroupBox starfieldBox1;
        private System.Windows.Forms.Label label1;
        private System.Windows.Forms.Label label2;
        private System.Windows.Forms.Button starfieldSelectFolder;
        private System.Windows.Forms.TextBox starfieldFolder;
        private System.Windows.Forms.ListBox progressListBox;
        private System.Windows.Forms.Button okButton;
    }
}