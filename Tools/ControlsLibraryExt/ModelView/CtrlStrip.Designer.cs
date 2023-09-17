// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

namespace ControlsLibraryExt.ModelView
{
    partial class CtrlStrip
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

        #region Component Designer generated code

        /// <summary> 
        /// Required method for Designer support - do not modify 
        /// the contents of this method with the code editor.
        /// </summary>
        private void InitializeComponent()
        {
            this._resetCam = new System.Windows.Forms.Button();
            this._colByMaterial = new System.Windows.Forms.ComboBox();
            this._skeletonMode = new System.Windows.Forms.ComboBox();
            this.label1 = new System.Windows.Forms.Label();
            this.visualizationRenderingButton = new System.Windows.Forms.Button();
            this.lightingRenderingButton = new System.Windows.Forms.Button();
            this._visualizationType = new System.Windows.Forms.ComboBox();
            this.label2 = new System.Windows.Forms.Label();
            this.label3 = new System.Windows.Forms.Label();
            this.label4 = new System.Windows.Forms.Label();
            this.cameraMode = new System.Windows.Forms.ComboBox();
            this.SuspendLayout();
            // 
            // _resetCam
            // 
            this._resetCam.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this._resetCam.Location = new System.Drawing.Point(1, 307);
            this._resetCam.Name = "_resetCam";
            this._resetCam.Size = new System.Drawing.Size(128, 23);
            this._resetCam.TabIndex = 1;
            this._resetCam.Text = "Reset Cam";
            this._resetCam.UseVisualStyleBackColor = true;
            this._resetCam.Click += new System.EventHandler(this.ResetCamClick);
            // 
            // _colByMaterial
            // 
            this._colByMaterial.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this._colByMaterial.DropDownStyle = System.Windows.Forms.ComboBoxStyle.DropDownList;
            this._colByMaterial.FormattingEnabled = true;
            this._colByMaterial.Location = new System.Drawing.Point(2, 154);
            this._colByMaterial.Name = "_colByMaterial";
            this._colByMaterial.Size = new System.Drawing.Size(128, 21);
            this._colByMaterial.TabIndex = 2;
            this._colByMaterial.SelectedIndexChanged += new System.EventHandler(this.SelectColorByMaterial);
            // 
            // _skeletonMode
            // 
            this._skeletonMode.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this._skeletonMode.DropDownStyle = System.Windows.Forms.ComboBoxStyle.DropDownList;
            this._skeletonMode.FormattingEnabled = true;
            this._skeletonMode.Location = new System.Drawing.Point(1, 232);
            this._skeletonMode.Name = "_skeletonMode";
            this._skeletonMode.Size = new System.Drawing.Size(128, 21);
            this._skeletonMode.TabIndex = 4;
            this._skeletonMode.SelectedIndexChanged += new System.EventHandler(this.SelectSkeletonMode);
            // 
            // label1
            // 
            this.label1.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.label1.AutoSize = true;
            this.label1.Location = new System.Drawing.Point(3, 3);
            this.label1.Margin = new System.Windows.Forms.Padding(3);
            this.label1.Name = "label1";
            this.label1.Size = new System.Drawing.Size(56, 13);
            this.label1.TabIndex = 5;
            this.label1.Text = "Rendering";
            // 
            // visualizationRenderingButton
            // 
            this.visualizationRenderingButton.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.visualizationRenderingButton.Location = new System.Drawing.Point(3, 22);
            this.visualizationRenderingButton.Name = "visualizationRenderingButton";
            this.visualizationRenderingButton.Size = new System.Drawing.Size(128, 23);
            this.visualizationRenderingButton.TabIndex = 6;
            this.visualizationRenderingButton.Text = "Visualization";
            this.visualizationRenderingButton.UseVisualStyleBackColor = true;
            this.visualizationRenderingButton.Click += new System.EventHandler(this.visualizationRenderingButton_Click);
            // 
            // lightingRenderingButton
            // 
            this.lightingRenderingButton.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.lightingRenderingButton.Location = new System.Drawing.Point(3, 78);
            this.lightingRenderingButton.Name = "lightingRenderingButton";
            this.lightingRenderingButton.Size = new System.Drawing.Size(128, 23);
            this.lightingRenderingButton.TabIndex = 7;
            this.lightingRenderingButton.Text = "Lighting";
            this.lightingRenderingButton.UseVisualStyleBackColor = true;
            this.lightingRenderingButton.Click += new System.EventHandler(this.lightingRenderingButton_Click);
            // 
            // _visualizationType
            // 
            this._visualizationType.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this._visualizationType.DropDownStyle = System.Windows.Forms.ComboBoxStyle.DropDownList;
            this._visualizationType.FormattingEnabled = true;
            this._visualizationType.Location = new System.Drawing.Point(3, 51);
            this._visualizationType.Name = "_visualizationType";
            this._visualizationType.Size = new System.Drawing.Size(128, 21);
            this._visualizationType.TabIndex = 8;
            this._visualizationType.SelectedIndexChanged += new System.EventHandler(this._visualizationType_SelectedIndexChanged);
            // 
            // label2
            // 
            this.label2.AutoSize = true;
            this.label2.Location = new System.Drawing.Point(3, 138);
            this.label2.Name = "label2";
            this.label2.Size = new System.Drawing.Size(100, 13);
            this.label2.TabIndex = 9;
            this.label2.Text = "Material highlighting";
            // 
            // label3
            // 
            this.label3.AutoSize = true;
            this.label3.Location = new System.Drawing.Point(2, 216);
            this.label3.Name = "label3";
            this.label3.Size = new System.Drawing.Size(75, 13);
            this.label3.TabIndex = 10;
            this.label3.Text = "Draw skeleton";
            // 
            // label4
            // 
            this.label4.AutoSize = true;
            this.label4.Location = new System.Drawing.Point(2, 291);
            this.label4.Name = "label4";
            this.label4.Size = new System.Drawing.Size(43, 13);
            this.label4.TabIndex = 11;
            this.label4.Text = "Camera";
            // 
            // cameraMode
            // 
            this.cameraMode.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this.cameraMode.DropDownStyle = System.Windows.Forms.ComboBoxStyle.DropDownList;
            this.cameraMode.FormattingEnabled = true;
            this.cameraMode.Location = new System.Drawing.Point(3, 336);
            this.cameraMode.Name = "cameraMode";
            this.cameraMode.Size = new System.Drawing.Size(127, 21);
            this.cameraMode.TabIndex = 12;
            // 
            // CtrlStrip
            // 
            this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.Controls.Add(this.cameraMode);
            this.Controls.Add(this.label4);
            this.Controls.Add(this.label3);
            this.Controls.Add(this.label2);
            this.Controls.Add(this._visualizationType);
            this.Controls.Add(this.lightingRenderingButton);
            this.Controls.Add(this.visualizationRenderingButton);
            this.Controls.Add(this.label1);
            this.Controls.Add(this._skeletonMode);
            this.Controls.Add(this._colByMaterial);
            this.Controls.Add(this._resetCam);
            this.Name = "CtrlStrip";
            this.Size = new System.Drawing.Size(134, 408);
            this.ResumeLayout(false);
            this.PerformLayout();

        }

        #endregion
        private System.Windows.Forms.Button _resetCam;
        private System.Windows.Forms.ComboBox _colByMaterial;
        private System.Windows.Forms.ComboBox _skeletonMode;
        private System.Windows.Forms.Label label1;
        private System.Windows.Forms.Button visualizationRenderingButton;
        private System.Windows.Forms.Button lightingRenderingButton;
        private System.Windows.Forms.ComboBox _visualizationType;
        private System.Windows.Forms.Label label2;
        private System.Windows.Forms.Label label3;
        private System.Windows.Forms.Label label4;
        private System.Windows.Forms.ComboBox cameraMode;
    }
}
