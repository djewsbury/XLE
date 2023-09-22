// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if false   // Disabled to separate dependency on DropDownControls.dll

using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Drawing;
using System.Data;
using System.Linq;
using System.Text;
using System.Windows.Forms;

namespace ControlsLibrary.MaterialEditor
{
    public partial class HierchicalMaterialControl : UserControl
    {
        public HierchicalMaterialControl()
        {
            InitializeComponent();
        }

        public string Object
        {
            set
            {
                    // Build a hierarchical of nodes in the combo box
                    // representing the inheritance tree in the material object
                ClearComboBoxNodes();
                if (value!=null) {
                    ComboTreeNode firstNode = null;

                    var split = value.Split(';').ToList();
                    for (int c = 0; c < split.Count; ++c)
                        firstNode = AddNode(split.GetRange(0, c+1));

                    _hierachyTree.SelectedNodeChanged += SubMatSelectedNodeChanged; 
                    _hierachyTree.SelectedNode = firstNode;
                }
                _activeObject = value;
            }
        }

        public MaterialControl FocusedMatControls { get { return _materialControl; } }

        public IEnumerable<GUILayer.RawMaterial> RawMaterialList
        {
            get
            {
                if (_activeObject != null)
                {
                    foreach (var mat in _activeObject.Split(';'))
                        yield return GUILayer.RawMaterial.Get(mat);
                }
            }
        }

        protected void SubMatSelectedNodeChanged(object sender, EventArgs e)
        {
                //  When the selected node changes, we want to 
                //  change the object that we're currently editing...
            var mat = (_hierachyTree.SelectedNode!=null) ? (_hierachyTree.SelectedNode.Tag as string) : null;
            _materialControl.Object = mat;
        }

        protected void ClearComboBoxNodes()
        {
            _hierachyTree.Nodes.Clear();
        }

        protected ComboTreeNode AddNode(IList<string> names)
        {
            string topItem = names[names.Count - 1];
            var parentNode = _hierachyTree.Nodes.Add(topItem);
            parentNode.Tag = topItem; // string.Join(";", names);
            // AddComboBoxChildren(parentNode, GUILayer.RawMaterial.Get(topItem).BuildInheritanceList());  (broken when inheritance mechanics moved to ResolvedAssetMixin)
            return parentNode;
        }

        protected void AddComboBoxChildren(ComboTreeNode parentNode, string childMats)
        {
            if (childMats == null || childMats.Length == 0) return;
            foreach (var mat in childMats.Split(';'))
            {
                var newNode = parentNode.Nodes.Add(mat);
                newNode.Tag = new List<string>() { mat };
                // AddComboBoxChildren(newNode, GUILayer.RawMaterial.Get(mat).BuildInheritanceList());  (broken when inheritance mechanics moved to ResolvedAssetMixin)
            }
        }

        public static class Prompt
        {
            public static string ShowDialog(string text, string caption)
            {
                // This little dialog biased on stack overflow article:
                // http://stackoverflow.com/questions/5427020/prompt-dialog-in-windows-forms

                Form prompt = new Form();
                prompt.Width = 500;
                prompt.Height = 150;
                prompt.FormBorderStyle = FormBorderStyle.FixedDialog;
                prompt.Text = caption;
                prompt.StartPosition = FormStartPosition.CenterScreen;
                Label textLabel = new Label() { Left = 50, Top = 20, Text = text };
                TextBox textBox = new TextBox() { Left = 50, Top = 50, Width = 400 };
                Button confirmation = new Button() { Text = "Ok", Left = 350, Width = 100, Top = 70, DialogResult = DialogResult.OK };
                Button cancel = new Button() { Text = "Cancel", Left = 225, Width = 100, Top = 70, DialogResult = DialogResult.OK };
                confirmation.Click += (sender, e) => { prompt.Close(); };
                prompt.Controls.Add(textBox);
                prompt.Controls.Add(confirmation);
                prompt.Controls.Add(textLabel);
                prompt.AcceptButton = confirmation;

                return prompt.ShowDialog() == DialogResult.OK ? textBox.Text : "";
            }
        }

        private void _addInherit_Click(object sender, EventArgs e)
        {
            var selectedMaterial = (_hierachyTree.SelectedNode != null) ? _hierachyTree.SelectedNode.Text : null;
            if (selectedMaterial == null) return;

            var result = Prompt.ShowDialog(
                "Material configuration:", 
                "Add Inheritted Material Settings");
            if (result.Length > 0)
            {
                //   (broken when inheritance mechanics moved to ResolvedAssetMixin)
                // using (var mat = GUILayer.RawMaterial.Get(selectedMaterial))
                //     mat.AddInherited(result);
                // now we need to refresh this control with the new changes...
                Object = _activeObject;
            }
        }

        private void _removeInherit_Click(object sender, EventArgs e)
        {}

        private string _activeObject;
    }
}

#endif
