using ControlsLibrary.MaterialEditor;
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

    [PartCreationPolicy(CreationPolicy.Shared)]
    [Export(typeof(IInitializable))]
    public class SimpleFileListerController : IControlHostClient, IInitializable
    {
        public TreeControlAdapter OpenResourceLister()
        {
            // the "TreeViewControl" and "TreeListViewControl" have different advantages
            //      TreeViewControl -- supports lazy loading, but no columns, also easier to navigate and expand
            //      TreeListViewControl -- supports columns, but no lazy loading (everything initialized at startup) and more difficult to expand folders
            var adapter = CreateTreeControlAdapter();
            var context = new ResourceSelectionTreeViewContext(GUILayer.ResourceFolderBridge.BeginFrom("starfield/data"), _resourceQueryService);
            adapter.TreeView = context;
            context.SelectionChanged += UpdatePreviewerContextAfterSelectionChange;

            _controlHostService.RegisterControl(
                adapter.TreeControl,
                new ControlInfo("File Lister", "File Lister", StandardControlGroup.Left),
                this);
            return adapter;
        }

        private void UpdatePreviewerContextAfterSelectionChange(object sender, EventArgs e)
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

                if ((resourceDesc.Value.Types & (uint)GUILayer.ResourceTypeFlags.Model) != 0)
                {
                    // It's a model extension. Pass it through to the current settings object
                    var modelSettings = previewerContext.ModelSettings;
                    modelSettings.ModelName = resourceDesc?.MountedName;
                    modelSettings.MaterialName = "";
                    modelSettings.Supplements = "";
                    previewerContext.ModelSettings = modelSettings;
                    // _settings.ResetCamera = true;
                }

                if ((resourceDesc.Value.Types & (uint)GUILayer.ResourceTypeFlags.Animation) != 0)
                {
                    var modelSettings = previewerContext.ModelSettings;
                    modelSettings.AnimationFileName = resourceDesc?.MountedName;
                    previewerContext.ModelSettings = modelSettings;
                }
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

        #region IControlHostClient
        void IControlHostClient.Activate(Control control) {}
        void IControlHostClient.Deactivate(Control control) {}
        bool IControlHostClient.Close(Control control) { return true;  }
        #endregion
    }
}
