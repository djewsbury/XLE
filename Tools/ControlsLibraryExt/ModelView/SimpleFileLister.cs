using ControlsLibrary.MaterialEditor;
using GUILayer;
using Sce.Atf;
using Sce.Atf.Adaptation;
using Sce.Atf.Applications;
using Sce.Atf.Controls;
using System;
using System.ComponentModel.Composition;
using System.ComponentModel.Composition.Hosting;
using System.Drawing;
using System.Security.Cryptography.X509Certificates;
using System.Windows.Forms;

namespace ControlsLibraryExt.ModelView
{
    public class CustomTreeItemRenderer : Sce.Atf.Controls.TreeItemRenderer
    {
        public override Size MeasureLabel(TreeControl.Node node, Graphics g)
        {
            Font defaultFont = GetDefaultFont(node, g);
            Size result = Size.Ceiling(g.MeasureString(node.Label, defaultFont));
            result.Height += 4;
            return result;
        }

        public override void DrawLabel(TreeControl.Node node, Graphics g, int x, int y)
        {
            Rectangle rectangle = new Rectangle(x, y+2, node.LabelWidth, node.LabelHeight-2);
            Brush brush = SystemBrushes.WindowText;
            Font defaultFont = GetDefaultFont(node, g);

            if (node.Selected)
            {
                Brush brush2 = (Owner.ContainsFocus ? HighlightBrush : DeactiveHighlightBrush);
                Brush brush3 = (Owner.ContainsFocus ? HighlightTextBrush : DeactiveHighlightTextBrush);
                Rectangle wideRectangle = new Rectangle(x-2, y, (int)g.ClipBounds.Right, node.LabelHeight);
                g.FillRectangle(brush2, wideRectangle);
                brush = brush3;
            }

            g.DrawString(node.Label, defaultFont, brush, rectangle);
        }
    }

    class FileListerOuterControl : UserControl
    {
        public event EventHandler AllFilesSelectionChanged;
        public event EventHandler ModelsSelectionChanged;
        public event EventHandler AnimationsSelectionChanged;
        public event EventHandler SkeletonsSelectionChanged;

        public FileListerOuterControl(string baseDir, GUILayer.TreeOfDirectoriesCalculator pendingTreeOfDirectories, GUILayer.IResourceQueryService resourceQueryService)
        {
            // the "TreeViewControl" and "TreeListViewControl" have different advantages
            //      TreeViewControl -- supports lazy loading, but no columns, also easier to navigate and expand
            //      TreeListViewControl -- supports columns, but no lazy loading (everything initialized at startup) and more difficult to expand folders
            _context[0] = new ResourceSelectionTreeViewContext(GUILayer.ResourceFolderBridge.BeginFrom(baseDir), resourceQueryService);
            _context[0].SelectionChanged += (object sender, EventArgs e) => AllFilesSelectionChanged.Invoke(sender, e);
            _pendingTreeOfDirectories = pendingTreeOfDirectories;
            _resourceQueryService = resourceQueryService;

            if (_pendingTreeOfDirectories.IsReady)
            {
                CompleteTreeOfDirectoriesConstruction();
            }
            else
            {
                _timer = new Timer();
                _timer.Tick += Timer_CheckTreeOfDirectories;
                _timer.Interval = 1000;
                _timer.Start();
            }

            this.SuspendLayout();

            ComboBox mode = new ComboBox();
            mode.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right;
            mode.DropDownStyle = ComboBoxStyle.DropDownList;
            mode.Size = new Size(this.Width, 23);
            mode.Items.Add("All Files");
            mode.Items.Add("Models");
            mode.Items.Add("Animations");
            mode.Items.Add("Skeletons");
            mode.SelectedIndex = 0;
            mode.SelectedIndexChanged += Mode_SelectedIndexChanged;

            var adapter = CreateTreeControlAdapter();
            adapter.TreeView = _context[0];
            _treeControls[0] = adapter.TreeControl;
            _treeControls[0].Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right | AnchorStyles.Bottom;
            _treeControls[0].Location = new Point(0, 23);
            _treeControls[0].Size = new Size(this.Width, this.Height-23);
            _currentMode = 0;

            _pendingMsg = new Label();
            _pendingMsg.Text = "Please wait -- filtering directory tree...";
            _pendingMsg.TextAlign = ContentAlignment.MiddleCenter;
            _pendingMsg.Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right | AnchorStyles.Bottom;
            _pendingMsg.Location = new Point(0, 23);
            _pendingMsg.Size = new Size(this.Width, this.Height - 23);

            this.Controls.Add(mode);
            this.Controls.Add(_treeControls[0]);
            this.ResumeLayout(false);
            this.PerformLayout();
        }

        protected virtual void Dispose(bool disposing)
        {
            if (!disposing) return;
            for (uint c = 0; c < 4; ++c)
            {
                if (_treeControls[c] != null)
                    _treeControls[c].Dispose();
                _treeControls[c] = null;
                if (_context[c] != null)
                    _context[c].Dispose();
                _context[c] = null;
                if (_checkBoxes[c] != null)
                    _checkBoxes[c].Dispose();
                _checkBoxes[c] = null;
            }
            if (_timer != null)
            {
                _timer.Stop();
                _timer.Dispose();
                _timer = null;
            }
        }

        private void Mode_SelectedIndexChanged(object sender, EventArgs e)
        {
            Controls.Remove(_treeControls[_currentMode]);
            if (_checkBoxes[_currentMode] != null)
                Controls.Remove(_checkBoxes[_currentMode]);
            Controls.Remove(_pendingMsg);

            int newMode = ((ComboBox)sender).SelectedIndex;
            if (newMode >= 0 && newMode < 4)
            {
                _currentMode = newMode;
                if (_treeControls[_currentMode] != null)
                {
                    Controls.Add(_treeControls[_currentMode]);
                    if (_checkBoxes[_currentMode] != null)
                        Controls.Add(_checkBoxes[_currentMode]);
                } else
                {
                    _pendingMsg.Location = new Point(0, 23);
                    _pendingMsg.Size = new Size(this.Width, this.Height - 23);
                    Controls.Add(_pendingMsg);
                }
            }
        }

        private TreeControlAdapter CreateTreeControlAdapter()
        {
            TreeControl treeControl = new TreeControl(TreeControl.Style.Tree, new CustomTreeItemRenderer());
            treeControl.SelectionMode = SelectionMode.One;
            treeControl.ImageList = ResourceUtil.GetImageList16();
            treeControl.StateImageList = ResourceUtil.GetImageList16();
            treeControl.ExpandOnSingleClick = true;
            return new TreeControlAdapter(treeControl);
        }

        private TreeListViewAdapter CreateTreeListView()
        {
            TreeListView treeControl = new TreeListView(TreeListView.Style.TreeList);
            // treeControl.SelectionMode = SelectionMode.One;
            treeControl.ImageList = ResourceUtil.GetImageList16();
            treeControl.StateImageList = ResourceUtil.GetImageList16();
            // treeControl.ExpandOnSingleClick = true;
            TreeListViewAdapter adapter = new TreeListViewAdapter(treeControl);
            // un-jankify the column widths
            foreach (var column in adapter.TreeListView.Columns)
                column.Width = 500;
            return adapter;
        }

        private void Timer_CheckTreeOfDirectories(object sender, EventArgs e)
        {
            if (_pendingTreeOfDirectories.IsReady)
            {
                CompleteTreeOfDirectoriesConstruction();
            }
        }

        private void CompleteTreeOfDirectoriesConstruction()
        {
            if (_timer != null)
            {
                _timer.Stop();
                _timer.Dispose();
                _timer = null;
            }

            _context[1] = new ResourceSelectionTreeViewContext(new GUILayer.ResourceFolderBridgeFromTreeOfDirectories(_pendingTreeOfDirectories, (uint)GUILayer.ResourceTypeFlags.Model), _resourceQueryService, (uint)GUILayer.ResourceTypeFlags.Model);
            _context[1].SelectionChanged += (object sender, EventArgs e) => ModelsSelectionChanged.Invoke(sender, e);

            _context[2] = new ResourceSelectionTreeViewContext(new GUILayer.ResourceFolderBridgeFromTreeOfDirectories(_pendingTreeOfDirectories, (uint)GUILayer.ResourceTypeFlags.Animation), _resourceQueryService, (uint)GUILayer.ResourceTypeFlags.Animation);
            _context[2].SelectionChanged += (object sender, EventArgs e) => AnimationsSelectionChanged.Invoke(sender, e);

            _context[3] = new ResourceSelectionTreeViewContext(new GUILayer.ResourceFolderBridgeFromTreeOfDirectories(_pendingTreeOfDirectories, (uint)GUILayer.ResourceTypeFlags.Skeleton), _resourceQueryService, (uint)GUILayer.ResourceTypeFlags.Skeleton);
            _context[3].SelectionChanged += (object sender, EventArgs e) => SkeletonsSelectionChanged.Invoke(sender, e);

            for (uint c = 1; c < 4; ++c)
            {
                var adapter = CreateTreeControlAdapter();
                adapter.TreeView = _context[c];
                _treeControls[c] = adapter.TreeControl;
                _treeControls[c].Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right | AnchorStyles.Bottom;

                if (c == 1)
                {
                    _treeControls[c].Location = new Point(0, 23);
                    _treeControls[c].Size = new Size(this.Width, this.Height - 23);
                }
                else if (c == 2 || c == 3)
                {
                    _treeControls[c].Location = new Point(0, 23 + 23);
                    _treeControls[c].Size = new Size(this.Width, this.Height - 23 - 23);

                    _checkBoxes[c] = new CheckBox();
                    _checkBoxes[c].Anchor = AnchorStyles.Top | AnchorStyles.Left | AnchorStyles.Right | AnchorStyles.Bottom;
                    _checkBoxes[c].Location = new Point(0, 23);
                    _checkBoxes[c].Size = new Size(this.Width, 23);
                }

                if (c == 2)
                {
                    _checkBoxes[c].Text = "Disable Animation";
                    _checkBoxes[c].CheckedChanged += (object sender, EventArgs e) =>
                    {
                        if (((CheckBox)sender).Checked)
                        {
                            _treeControls[2].Enabled = false;
                            AnimationsSelectionChanged.Invoke(null, null);
                        }
                        else
                        {
                            // fake a selection changed event from the tree control
                            _treeControls[2].Enabled = true;
                            AnimationsSelectionChanged.Invoke(_context[2], null);
                        }
                    };
                }
                else if (c == 3)
                {
                    _checkBoxes[c].Text = "Use Embedded Skeleton";
                    _checkBoxes[c].CheckedChanged += (object sender, EventArgs e) =>
                    {
                        if (((CheckBox)sender).Checked)
                        {
                            _treeControls[3].Enabled = false; 
                            SkeletonsSelectionChanged.Invoke(null, null);
                        }
                        else
                        {
                            // fake a selection changed event from the tree control
                            _treeControls[3].Enabled = true;
                            SkeletonsSelectionChanged.Invoke(_context[3], null);
                        }
                    };
                }
            }

            if (_currentMode >= 1 && _currentMode < 4)
            {
                Controls.Remove(_pendingMsg);
                Controls.Add(_treeControls[_currentMode]);
                if (_checkBoxes[_currentMode] != null)
                    Controls.Add(_checkBoxes[_currentMode]);
            }
        }

        private TreeControl[] _treeControls = new TreeControl[4] { null, null, null, null };
        private CheckBox[] _checkBoxes = new CheckBox[4] { null, null, null, null };
        private ResourceSelectionTreeViewContext[] _context = new ResourceSelectionTreeViewContext[4] { null, null, null, null };
        private int _currentMode = 0;
        private Label _pendingMsg;

        private GUILayer.TreeOfDirectoriesCalculator _pendingTreeOfDirectories;
        private GUILayer.IResourceQueryService _resourceQueryService;
        private Timer _timer = null;
    }

    [PartCreationPolicy(CreationPolicy.Shared)]
    [Export(typeof(IInitializable))]
    public class SimpleFileListerHost : IControlHostClient, IInitializable
    {
        public void OpenResourceLister()
        {
            if (_pendingTreeOfDirectories == null)
                _pendingTreeOfDirectories = new GUILayer.TreeOfDirectoriesCalculator("starfield/data");

            FileListerOuterControl ctrl = new FileListerOuterControl("starfield/data", _pendingTreeOfDirectories, _resourceQueryService);
            ctrl.AllFilesSelectionChanged += (object sender, EventArgs e) => UpdatePreviewerContextAfterSelectionChange_Internal(sender, e, ~0u);
            ctrl.ModelsSelectionChanged += (object sender, EventArgs e) => UpdatePreviewerContextAfterSelectionChange_Internal(sender, e, (uint)GUILayer.ResourceTypeFlags.Model);
            ctrl.AnimationsSelectionChanged += (object sender, EventArgs e) => UpdatePreviewerContextAfterSelectionChange_Internal(sender, e, (uint)GUILayer.ResourceTypeFlags.Animation);
            ctrl.SkeletonsSelectionChanged += (object sender, EventArgs e) => UpdatePreviewerContextAfterSelectionChange_Internal(sender, e, (uint)GUILayer.ResourceTypeFlags.Skeleton);

            _controlHostService.RegisterControl(
                ctrl,
                new ControlInfo("File Lister", "File Lister", StandardControlGroup.Left),
                this);
        }

        private void UpdatePreviewerContextAfterSelectionChange_Internal(object sender, EventArgs e, uint typeFilter)
        {
            // find a preview context we can update
            PreviewerContext previewerContext = null;
            foreach (var control in _controlHostService.Controls)
            {
                var adaptableControl = control.Control as Sce.Atf.Controls.Adaptable.AdaptableControl;
                if (adaptableControl != null)
                {
                    var c = adaptableControl.ContextAs<PreviewerContext>();
                    if (c != null) previewerContext = c;
                }
            }

            if (previewerContext != null)
            {
                if (sender != null)
                {
                    var sel = (sender as ResourceSelectionTreeViewContext).LastSelected.As<Array>();
                    if (sel == null) return;
                    var resourceDesc = _resourceQueryService.GetDesc(sel);
                    if (resourceDesc == null) return;

                    bool changedSomething = false;
                    if ((resourceDesc.Value.Types & typeFilter & (uint)GUILayer.ResourceTypeFlags.Model) != 0)
                    {
                        // It's a model extension. Pass it through to the current settings object
                        var modelSettings = previewerContext.ModelSettings;
                        modelSettings.ModelName = resourceDesc?.MountedName;
                        modelSettings.MaterialName = "";
                        modelSettings.Supplements = "";
                        if ((typeFilter & (uint)GUILayer.ResourceTypeFlags.Skeleton) != 0)
                            modelSettings.SkeletonFileName = "";
                        // _settings.ResetCamera = true;
                        changedSomething = true;
                    }

                    if ((resourceDesc.Value.Types & typeFilter & (uint)GUILayer.ResourceTypeFlags.Skeleton) != 0)
                    {
                        if (!changedSomething)  // don't set this if we just bound is as our model
                        {
                            var modelSettings = previewerContext.ModelSettings;
                            modelSettings.SkeletonFileName = resourceDesc?.MountedName;
                            changedSomething = true;
                        }
                    }

                    if ((resourceDesc.Value.Types & typeFilter & (uint)GUILayer.ResourceTypeFlags.Animation) != 0)
                    {
                        var modelSettings = previewerContext.ModelSettings;
                        modelSettings.AnimationFileName = resourceDesc?.MountedName;
                        changedSomething = true;
                    }

                    if (changedSomething)
                        previewerContext.ModelSettings = previewerContext.ModelSettings;
                }
                else
                {
                    // In this mode, we're just clearing out the resource of the specified type
                    bool changedSomething = false;
                    if ((typeFilter & (uint)GUILayer.ResourceTypeFlags.Model) != 0)
                    {
                        previewerContext.ModelSettings.ModelName = "";
                        changedSomething = true;
                    }

                    if ((typeFilter & (uint)GUILayer.ResourceTypeFlags.Skeleton) != 0)
                    {
                        previewerContext.ModelSettings.SkeletonFileName = "";
                        changedSomething = true;
                    }

                    if ((typeFilter & (uint)GUILayer.ResourceTypeFlags.Animation) != 0)
                    {
                        previewerContext.ModelSettings.AnimationFileName = "";
                        changedSomething = true;
                    }

                    if (changedSomething)
                        previewerContext.ModelSettings = previewerContext.ModelSettings;
                }
            }
        }

        void IInitializable.Initialize()
        {
            OpenResourceLister();
        }

        [Import(AllowDefault = true)]
        private GUILayer.IResourceQueryService _resourceQueryService;

        [Import(AllowDefault = false)]
        private ControlHostService _controlHostService = null;

        GUILayer.TreeOfDirectoriesCalculator _pendingTreeOfDirectories = null;

        #region IControlHostClient
        void IControlHostClient.Activate(Control control) {}
        void IControlHostClient.Deactivate(Control control) {}
        bool IControlHostClient.Close(Control control) { return true;  }
        #endregion
    }
}
