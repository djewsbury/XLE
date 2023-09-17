namespace NodeEditorCore
{
    partial class SubGraphPropertiesForm
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
            this._okButton = new System.Windows.Forms.Button();
            this._cancelButton = new System.Windows.Forms.Button();
            this._nameBox = new System.Windows.Forms.TextBox();
            this.label1 = new System.Windows.Forms.Label();
            this._implementsBox = new System.Windows.Forms.ComboBox();
            this.label2 = new System.Windows.Forms.Label();
            this.SuspendLayout();
            // 
            // _okButton
            // 
            this._okButton.Anchor = ((System.Windows.Forms.AnchorStyles)((System.Windows.Forms.AnchorStyles.Bottom | System.Windows.Forms.AnchorStyles.Right)));
            this._okButton.DialogResult = System.Windows.Forms.DialogResult.OK;
            this._okButton.Location = new System.Drawing.Point(478, 50);
            this._okButton.Margin = new System.Windows.Forms.Padding(1, 2, 1, 2);
            this._okButton.Name = "_okButton";
            this._okButton.Size = new System.Drawing.Size(75, 22);
            this._okButton.TabIndex = 0;
            this._okButton.Text = "OK";
            this._okButton.UseVisualStyleBackColor = true;
            // 
            // _cancelButton
            // 
            this._cancelButton.Anchor = ((System.Windows.Forms.AnchorStyles)((System.Windows.Forms.AnchorStyles.Bottom | System.Windows.Forms.AnchorStyles.Right)));
            this._cancelButton.DialogResult = System.Windows.Forms.DialogResult.Cancel;
            this._cancelButton.Location = new System.Drawing.Point(401, 50);
            this._cancelButton.Margin = new System.Windows.Forms.Padding(1, 2, 1, 2);
            this._cancelButton.Name = "_cancelButton";
            this._cancelButton.Size = new System.Drawing.Size(75, 22);
            this._cancelButton.TabIndex = 1;
            this._cancelButton.Text = "Cancel";
            this._cancelButton.UseVisualStyleBackColor = true;
            // 
            // _nameBox
            // 
            this._nameBox.Location = new System.Drawing.Point(5, 21);
            this._nameBox.Margin = new System.Windows.Forms.Padding(1, 2, 1, 2);
            this._nameBox.Name = "_nameBox";
            this._nameBox.Size = new System.Drawing.Size(146, 20);
            this._nameBox.TabIndex = 2;
            // 
            // label1
            // 
            this.label1.AutoSize = true;
            this.label1.Location = new System.Drawing.Point(6, 5);
            this.label1.Margin = new System.Windows.Forms.Padding(1, 0, 1, 0);
            this.label1.Name = "label1";
            this.label1.Size = new System.Drawing.Size(35, 13);
            this.label1.TabIndex = 3;
            this.label1.Text = "Name";
            // 
            // _implementsBox
            // 
            this._implementsBox.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this._implementsBox.FormattingEnabled = true;
            this._implementsBox.Items.AddRange(new object[] {
            "",
            "xleres/Objects/Templates.pixel.hlsl:CoordinatesToColor",
            "xleres/Objects/Templates.pixel.hlsl:PerPixel",
            "xleres/Objects/Templates.pixel.hlsl:EarlyRejectionTest"});
            this._implementsBox.Location = new System.Drawing.Point(153, 21);
            this._implementsBox.Margin = new System.Windows.Forms.Padding(1, 2, 1, 2);
            this._implementsBox.Name = "_implementsBox";
            this._implementsBox.Size = new System.Drawing.Size(402, 21);
            this._implementsBox.TabIndex = 4;
            // 
            // label2
            // 
            this.label2.AutoSize = true;
            this.label2.Location = new System.Drawing.Point(153, 4);
            this.label2.Margin = new System.Windows.Forms.Padding(1, 0, 1, 0);
            this.label2.Name = "label2";
            this.label2.Size = new System.Drawing.Size(60, 13);
            this.label2.TabIndex = 5;
            this.label2.Text = "Implements";
            // 
            // SubGraphPropertiesForm
            // 
            this.AcceptButton = this._okButton;
            this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.CancelButton = this._cancelButton;
            this.ClientSize = new System.Drawing.Size(558, 79);
            this.ControlBox = false;
            this.Controls.Add(this.label2);
            this.Controls.Add(this._implementsBox);
            this.Controls.Add(this.label1);
            this.Controls.Add(this._nameBox);
            this.Controls.Add(this._cancelButton);
            this.Controls.Add(this._okButton);
            this.Margin = new System.Windows.Forms.Padding(1, 2, 1, 2);
            this.MaximizeBox = false;
            this.MinimizeBox = false;
            this.Name = "SubGraphPropertiesForm";
            this.ShowIcon = false;
            this.ShowInTaskbar = false;
            this.SizeGripStyle = System.Windows.Forms.SizeGripStyle.Hide;
            this.Text = "Sub Graph Properties";
            this.ResumeLayout(false);
            this.PerformLayout();

        }

        #endregion

        private System.Windows.Forms.Button _okButton;
        private System.Windows.Forms.Button _cancelButton;
        private System.Windows.Forms.TextBox _nameBox;
        private System.Windows.Forms.Label label1;
        private System.Windows.Forms.ComboBox _implementsBox;
        private System.Windows.Forms.Label label2;
    }
}