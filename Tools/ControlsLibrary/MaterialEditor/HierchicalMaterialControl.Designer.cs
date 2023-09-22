// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if false   // Disabled to separate dependency on DropDownControls.dll

namespace ControlsLibrary.MaterialEditor
{
    partial class HierchicalMaterialControl
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
            if (disposing)
            {
                    //      ClearComboBoxNodes causes a crash (indirectly)
                    //      if the user is currently editing one of the items
                    //      in a DataGrid in MaterialControl
                // ClearComboBoxNodes();
                _materialControl.Dispose();
                _hierachyTree.Dispose();
                if (components != null)
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
            this._splitContainer1 = new System.Windows.Forms.SplitContainer();
            this._removeInherit = new System.Windows.Forms.Button();
            this._addInherit = new System.Windows.Forms.Button();
            this._hierachyTree = new ComboTreeBox();
            this._materialControl = new MaterialControl();
            this._materialControl.SuspendLayout();
            ((System.ComponentModel.ISupportInitialize)(this._splitContainer1)).BeginInit();
            this._splitContainer1.Panel1.SuspendLayout();
            this._splitContainer1.Panel2.SuspendLayout();
            this._splitContainer1.SuspendLayout();
            this.SuspendLayout();
            // 
            // _splitContainer1
            // 
            this._splitContainer1.Dock = System.Windows.Forms.DockStyle.Fill;
            this._splitContainer1.FixedPanel = System.Windows.Forms.FixedPanel.Panel1;
            this._splitContainer1.IsSplitterFixed = true;
            this._splitContainer1.Location = new System.Drawing.Point(0, 0);
            this._splitContainer1.Name = "_splitContainer1";
            this._splitContainer1.Orientation = System.Windows.Forms.Orientation.Horizontal;
            // 
            // _splitContainer1.Panel1
            // 
            this._splitContainer1.Panel1.Controls.Add(this._removeInherit);
            this._splitContainer1.Panel1.Controls.Add(this._addInherit);
            this._splitContainer1.Panel1.Controls.Add(this._hierachyTree);
            this._splitContainer1.Panel1.RightToLeft = System.Windows.Forms.RightToLeft.No;
            // 
            // _splitContainer1.Panel2
            // 
            this._splitContainer1.Panel2.Controls.Add(this._materialControl);
            this._splitContainer1.Panel2.RightToLeft = System.Windows.Forms.RightToLeft.No;
            this._splitContainer1.Size = new System.Drawing.Size(297, 568);
            this._splitContainer1.SplitterDistance = 25;
            this._splitContainer1.TabIndex = 0;
            // 
            // _removeInherit
            // 
            this._removeInherit.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this._removeInherit.Location = new System.Drawing.Point(271, 0);
            this._removeInherit.Name = "_removeInherit";
            this._removeInherit.Size = new System.Drawing.Size(26, 25);
            this._removeInherit.TabIndex = 2;
            this._removeInherit.Text = "-";
            this._removeInherit.UseVisualStyleBackColor = true;
            this._removeInherit.Click += new System.EventHandler(this._removeInherit_Click);
            // 
            // _addInherit
            // 
            this._addInherit.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this._addInherit.Location = new System.Drawing.Point(239, 0);
            this._addInherit.Name = "_addInherit";
            this._addInherit.Size = new System.Drawing.Size(26, 25);
            this._addInherit.TabIndex = 1;
            this._addInherit.Text = "+";
            this._addInherit.UseVisualStyleBackColor = true;
            this._addInherit.Click += new System.EventHandler(this._addInherit_Click);
            // 
            // _hierachyTree
            // 
            this._hierachyTree.Anchor = ((System.Windows.Forms.AnchorStyles)((((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom) 
            | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
            this._hierachyTree.DroppedDown = false;
            this._hierachyTree.Location = new System.Drawing.Point(0, 0);
            this._hierachyTree.Name = "_hierachyTree";
            this._hierachyTree.SelectedNode = null;
            this._hierachyTree.Size = new System.Drawing.Size(233, 25);
            this._hierachyTree.TabIndex = 0;
            // 
            // _materialControl
            // 
            this._materialControl.Dock = System.Windows.Forms.DockStyle.Fill;
            this._materialControl.Location = new System.Drawing.Point(0, 0);
            this._materialControl.Name = "_materialControl";
            this._materialControl.Size = new System.Drawing.Size(297, 539);
            this._materialControl.TabIndex = 0;
            // 
            // HierchicalMaterialControl
            // 
            this.Controls.Add(this._splitContainer1);
            this.Name = "HierchicalMaterialControl";
            this.Size = new System.Drawing.Size(297, 568);
            this._materialControl.ResumeLayout(true);
            this._splitContainer1.Panel1.ResumeLayout(false);
            this._splitContainer1.Panel2.ResumeLayout(false);
            ((System.ComponentModel.ISupportInitialize)(this._splitContainer1)).EndInit();
            this._splitContainer1.ResumeLayout(false);
            this.ResumeLayout(false);

        }

        #endregion

        private System.Windows.Forms.SplitContainer _splitContainer1;
        private ComboTreeBox _hierachyTree;
        private MaterialControl _materialControl;
        private System.Windows.Forms.Button _removeInherit;
        private System.Windows.Forms.Button _addInherit;
    }
}

#endif
