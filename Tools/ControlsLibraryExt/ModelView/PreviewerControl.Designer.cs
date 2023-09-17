// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

namespace ControlsLibraryExt.ModelView
{
    partial class PreviewerControl
    {
        /// <summary> 
        /// Required designer variable.
        /// </summary>
        private System.ComponentModel.IContainer components = null;

        #region Component Designer generated code

        /// <summary> 
        /// Required method for Designer support - do not modify 
        /// the contents of this method with the code editor.
        /// </summary>
        private void InitializeComponent()
        {
            this._ctrls = new CtrlStrip();
            this._animationCtrls = new AnimationCtrlStrip();
            this._view = new ControlsLibrary.LayerControl();
            this.SuspendLayout();
            // 
            // _ctrls
            // 
            this._ctrls.Anchor = (System.Windows.Forms.AnchorStyles)(System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Right);
            this._ctrls.Location = new System.Drawing.Point(543, 0);
            this._ctrls.Size = new System.Drawing.Size(134, 408);
            this._ctrls.TabIndex = 1;
            // 
            // _animationCtrls
            // 
            this._animationCtrls.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Bottom | System.Windows.Forms.AnchorStyles.Left)
            | System.Windows.Forms.AnchorStyles.Right)));
            this._animationCtrls.Location = new System.Drawing.Point(0, 550);
            this._animationCtrls.Size = new System.Drawing.Size(677, 32);
            this._animationCtrls.TabIndex = 2;
            // 
            // _view
            // 
            this._view.Anchor = ((System.Windows.Forms.AnchorStyles)((((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom) 
            | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this._view.Location = new System.Drawing.Point(0, 0);
            this._view.Size = new System.Drawing.Size(543, 550);
            this._view.TabIndex = 0;
            // 
            // PreviewerControl
            // 
            // this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
            // this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.Controls.Add(this._view);
            this.Controls.Add(this._ctrls);
            this.Controls.Add(this._animationCtrls);
            this.Margin = new System.Windows.Forms.Padding(0);
            this.Name = "PreviewerControl";
            this.Size = new System.Drawing.Size(677, 582);
            this.ResumeLayout(false);

        }

        #endregion

        private CtrlStrip _ctrls;
        private AnimationCtrlStrip _animationCtrls;
        private ControlsLibrary.LayerControl _view;
    }
}
