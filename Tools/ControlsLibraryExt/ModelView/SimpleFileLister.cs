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
        public FileListerOuterControl(ResourceSelectionTreeViewContext allFilesContext, ResourceSelectionTreeViewContext modelFilesContext, ResourceSelectionTreeViewContext animationFilesContext, ResourceSelectionTreeViewContext skeletonFilesContext)
        {
            _allFilesContext = allFilesContext;
            _modelFilesContext = modelFilesContext; 
            _animationFilesContext = animationFilesContext;
            _skeletonFilesContext = skeletonFilesContext;

            this.SuspendLayout();

            ComboBox mode = new ComboBox();
            mode.Anchor = (System.Windows.Forms.AnchorStyles)(System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left | System.Windows.Forms.AnchorStyles.Right);
            mode.DropDownStyle = System.Windows.Forms.ComboBoxStyle.DropDownList;
            mode.Size = new System.Drawing.Size(this.Width, 23);
            mode.Items.Add("All Files");
            mode.Items.Add("Models");
            mode.Items.Add("Animations");
            mode.Items.Add("Skeletons");
            mode.SelectedIndex = 0;
            mode.SelectedIndexChanged += Mode_SelectedIndexChanged;

            var adapter = CreateTreeControlAdapter();
            adapter.TreeView = allFilesContext;
            _currentTreeControl = adapter.TreeControl;
            _currentTreeControl.Anchor = (System.Windows.Forms.AnchorStyles)(System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left | System.Windows.Forms.AnchorStyles.Right | System.Windows.Forms.AnchorStyles.Bottom);
            _currentTreeControl.Location = new System.Drawing.Point(0, 23);
            _currentTreeControl.Size = new System.Drawing.Size(this.Width, this.Height-23);

            this.Controls.Add(mode);
            this.Controls.Add(_currentTreeControl);
            this.ResumeLayout(false);
            this.PerformLayout();
        }

        protected virtual void Dispose(bool disposing)
        {
            if (!disposing) return;
            if (_currentTreeControl != null)
                _currentTreeControl.Dispose();
            _currentTreeControl = null;
            _allFilesContext = null;
            _animationFilesContext = null;
        }

        private void Mode_SelectedIndexChanged(object sender, EventArgs e)
        {
            if (_currentTreeControl != null)
            {
                Controls.Remove(_currentTreeControl);
                _currentTreeControl.Dispose();
                _currentTreeControl = null;
            }

            int newMode = ((ComboBox)sender).SelectedIndex;
            if (newMode == 0)
            {
                var adapter = CreateTreeControlAdapter();
                adapter.TreeView = _allFilesContext;
                _currentTreeControl = adapter.TreeControl;
            }
            else if (newMode == 1)
            {
                var adapter = CreateTreeControlAdapter();
                adapter.TreeView = _modelFilesContext;
                _currentTreeControl = adapter.TreeControl;
            }
            else if (newMode == 2)
            {
                var adapter = CreateTreeControlAdapter();
                adapter.TreeView = _animationFilesContext;
                _currentTreeControl = adapter.TreeControl;
            }
            else if (newMode == 3)
            {
                var adapter = CreateTreeControlAdapter();
                adapter.TreeView = _skeletonFilesContext;
                _currentTreeControl = adapter.TreeControl;
            }

            if (_currentTreeControl != null)
            {
                _currentTreeControl.Anchor = (System.Windows.Forms.AnchorStyles)(System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left | System.Windows.Forms.AnchorStyles.Right | System.Windows.Forms.AnchorStyles.Bottom);
                _currentTreeControl.Location = new System.Drawing.Point(0, 23);
                _currentTreeControl.Size = new System.Drawing.Size(this.Width, this.Height - 23);
                Controls.Add(_currentTreeControl);
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

        private TreeControl _currentTreeControl;
        private ResourceSelectionTreeViewContext _allFilesContext;
        private ResourceSelectionTreeViewContext _modelFilesContext;
        private ResourceSelectionTreeViewContext _animationFilesContext;
        private ResourceSelectionTreeViewContext _skeletonFilesContext;
    }

    [PartCreationPolicy(CreationPolicy.Shared)]
    [Export(typeof(IInitializable))]
    public class SimpleFileListerController : IControlHostClient, IInitializable
    {
        public void OpenResourceLister()
        {
            // the "TreeViewControl" and "TreeListViewControl" have different advantages
            //      TreeViewControl -- supports lazy loading, but no columns, also easier to navigate and expand
            //      TreeListViewControl -- supports columns, but no lazy loading (everything initialized at startup) and more difficult to expand folders
            var allFilesContext = new ResourceSelectionTreeViewContext(GUILayer.ResourceFolderBridge.BeginFrom("starfield/data"), _resourceQueryService);
            var modelFilesContext = new ResourceSelectionTreeViewContext(GUILayer.ResourceFolderBridge.BeginFrom("starfield/data"), _resourceQueryService, (uint)GUILayer.ResourceTypeFlags.Model); 
            var animationFilesContext = new ResourceSelectionTreeViewContext(GUILayer.ResourceFolderBridge.BeginFrom("starfield/data"), _resourceQueryService, (uint)GUILayer.ResourceTypeFlags.Animation);
            var skeletonFilesContext = new ResourceSelectionTreeViewContext(GUILayer.ResourceFolderBridge.BeginFrom("starfield/data"), _resourceQueryService, (uint)GUILayer.ResourceTypeFlags.Skeleton);
            allFilesContext.SelectionChanged += (object sender, EventArgs e) => UpdatePreviewerContextAfterSelectionChange_Internal(sender, e, ~0u);
            modelFilesContext.SelectionChanged += (object sender, EventArgs e) => UpdatePreviewerContextAfterSelectionChange_Internal(sender, e, (uint)GUILayer.ResourceTypeFlags.Model);
            animationFilesContext.SelectionChanged += (object sender, EventArgs e) => UpdatePreviewerContextAfterSelectionChange_Internal(sender, e, (uint)GUILayer.ResourceTypeFlags.Animation);
            skeletonFilesContext.SelectionChanged += (object sender, EventArgs e) => UpdatePreviewerContextAfterSelectionChange_Internal(sender, e, (uint)GUILayer.ResourceTypeFlags.Skeleton);
            FileListerOuterControl ctrl = new FileListerOuterControl(allFilesContext, modelFilesContext, animationFilesContext, skeletonFilesContext);

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
        }

        void IInitializable.Initialize()
        {
            OpenResourceLister();
        }

        public ResourceSelectionTreeViewContext ResourceSelectionContext
        {
            get
            {
                if (_resourceSelectionContext == null)
                {
                    var root = GUILayer.ResourceFolderBridge.BeginFromRoot();
                    _resourceSelectionContext = new ResourceSelectionTreeViewContext(root, _resourceQueryService);
                }
                return _resourceSelectionContext;
            }
        }
        private ResourceSelectionTreeViewContext _resourceSelectionContext = null;

        [Import(AllowDefault = true)]
        private GUILayer.IResourceQueryService _resourceQueryService;

        [Import(AllowDefault = false)]
        private ControlHostService _controlHostService = null;

        #region IControlHostClient
        void IControlHostClient.Activate(Control control) {}
        void IControlHostClient.Deactivate(Control control) {}
        bool IControlHostClient.Close(Control control) { return true;  }
        #endregion
    }
}
