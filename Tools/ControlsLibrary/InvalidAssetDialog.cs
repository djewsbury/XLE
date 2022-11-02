using System;
using System.Collections;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Windows.Forms;

namespace ControlsLibrary
{
    public partial class InvalidAssetDialog : Form
    {
        public InvalidAssetDialog(GUILayer.InvalidAssetList invalidAssetList)
        {
            _list = invalidAssetList;
            InitializeComponent();

            RebuildList();
            invalidAssetList._onChange += RebuildList;

            _assetList.SelectedIndexChanged += SelectedIndexChanged;
        }

        void SelectedIndexChanged(object sender, EventArgs e)
        {
            _errorDetails.Clear();
            foreach (var a in _list.AssetList)
                if (a.Item1.Equals(_assetList.SelectedItem))
                    _errorDetails.Lines = a.Item2.Split(new string[] { "\n", "\r\n" }, StringSplitOptions.None);
        }

        private void _closeButton_Click(object sender, EventArgs e)
        {
            DialogResult = DialogResult.OK;
        }

        private void RebuildList()
        {
            if (InvokeRequired) {
                BeginInvoke(new Action(() => this.RebuildList()));
                return;
            }

            _assetList.Items.Clear();
            var list = _list.AssetList;
            foreach (var a in list)
                _assetList.Items.Add(a.Item1);
            if (_assetList.Items.Count > 0)
                _assetList.SelectedIndex = 0;
        }

        protected GUILayer.InvalidAssetList _list;
    }
}
