//Copyright © 2014 Sony Computer Entertainment America LLC. See License.txt.

using System;
using System.Collections.Generic;
using System.ComponentModel.Composition;
using System.Drawing;
using System.IO;
using System.Windows.Forms;
using System.Threading;

using Sce.Atf;
using Sce.Atf.Applications;
using Sce.Atf.Adaptation;
using Sce.Atf.Controls;
using Sce.Atf.Controls.PropertyEditing;

namespace LevelEditorCore
{
    /// <summary>
    /// GUI component for browsing and organizing resource folders and resources (e.g., models, images, etc.)</summary>
    /// <remarks>Similar to Windows Explorer, this editor contains a folder tree and a contents view area.
    /// The contents view can be switched between details and thumbnails view. Replaces ATF 2's AssetLister.</remarks>
    [Export(typeof(ResourceLister))]
    [Export(typeof(IInitializable))]
    [PartCreationPolicy(CreationPolicy.Shared)]
    public class ResourceLister : ICommandClient, IControlHostClient, IInitializable, IDisposable
    {
        /// <summary>
        /// Event that is raised after the selection changes</summary>
        public event EventHandler SelectionChanged = delegate { };

        /// <summary>
        /// Sets the root resource folder, registers events and refreshes controls</summary>
        /// <param name="rootFolder">Resource folder</param>
        public void SetRootFolder(IOpaqueResourceFolder rootFolder)
        {

            if (m_watcher == null)
            {
                m_uiThreadContext = SynchronizationContext.Current;
                m_watcher = new FileSystemWatcher();
                m_watcher.SynchronizingObject = m_mainForm;
                m_watcher.IncludeSubdirectories = true;
                m_watcher.EnableRaisingEvents = false;
                m_watcher.Created += Watcher_FileChanged;
                m_watcher.Deleted += Watcher_FileChanged;
                m_watcher.Renamed += Watcher_Renamed;
            }

            IFileSystemResourceFolder rootDirectory = rootFolder as IFileSystemResourceFolder;
            if (rootDirectory != null)
            {
                m_watcher.Path = rootDirectory.FullPath;
                m_watcher.EnableRaisingEvents = true;
            }
            else
            {
                m_watcher.Path = null;
                m_watcher.EnableRaisingEvents = false;
            }
                            
            if (m_treeContext != null)
            {
                m_treeContext.SelectionChanged -= TreeSelectionChanged;
            }

            m_treeContext = new TreeViewContext(rootFolder);
            m_treeContext.SelectionChanged += TreeSelectionChanged;
            m_treeControlAdapter.TreeView = m_treeContext;

            if (m_listContext != null)
                m_listContext.SelectionChanged -= listSelectionContext_SelectionChanged;

            m_listContext = new ListViewContext();
            m_listContext.SelectionChanged += listSelectionContext_SelectionChanged;
            m_listContext.ResourceQueryService = m_resourceQueryService;
            m_listViewAdapter.ListView = m_listContext;

            m_treeControlAdapter.Refresh(rootFolder);
        }

        private void Watcher_Renamed(object sender, RenamedEventArgs e)
        {
                // this function interacts with m_treeControlAdapter in way that
                // might not be thread safe. We can push this functionality into the
                // ui thread, so we don't get any race conditions while rendering or
                // shutting down.
            if (m_uiThreadContext != SynchronizationContext.Current)
            {
                m_uiThreadContext.Post(delegate { Watcher_Renamed(sender, e); }, null);
                return;
            }


            var attr = File.GetAttributes(e.FullPath);
            bool isDirectory = (attr & FileAttributes.Directory) == FileAttributes.Directory;            
            IFileSystemResourceFolder folder = m_treeContext.GetLastSelected<IFileSystemResourceFolder>();
            string selectedFolder = folder == null ? string.Empty : GetNormalizedName(folder.FullPath);
                       
            if (isDirectory)
            {//                 
                m_treeControlAdapter.Refresh(m_treeContext.RootFolder);                
            }
            else // it is a file
            {
                string dirName = Path.GetDirectoryName(e.OldFullPath);
                dirName = GetNormalizedName(dirName);
                if (selectedFolder == dirName)
                {
                    TreeSelectionChanged(m_treeContext, EventArgs.Empty);
                }
            }            
        }

        /// <summary>
        /// Handler for file or folder created or deleted.</summary>
        /// <param name="sender"></param>
        /// <param name="e"></param>
        private void Watcher_FileChanged(object sender, FileSystemEventArgs e)
        {
                // this function interacts with m_treeControlAdapter in way that
                // might not be thread safe. We can push this functionality into the
                // ui thread, so we don't get any race conditions while rendering or
                // shutting down.
            if (m_uiThreadContext != SynchronizationContext.Current)
            {
                m_uiThreadContext.Post(delegate { Watcher_FileChanged(sender, e); }, null);
                return;
            }

            try
            {
                IFileSystemResourceFolder folder = m_treeContext.GetLastSelected<IFileSystemResourceFolder>();
                string selectedFolder = folder == null ? string.Empty : GetNormalizedName(folder.FullPath);
                string parentDirName = Path.GetDirectoryName(e.FullPath);
                parentDirName = GetNormalizedName(parentDirName);
                string itemName = GetNormalizedName(e.FullPath);
                if ((e.ChangeType & WatcherChangeTypes.Created) == WatcherChangeTypes.Created)
                {

                    var attr = File.GetAttributes(e.FullPath);
                    bool isDirectory = (attr & FileAttributes.Directory) == FileAttributes.Directory;
                    if (isDirectory)
                    {
                        m_treeControlAdapter.Refresh(m_treeContext.RootFolder);
                    }
                    else if (parentDirName == selectedFolder)
                    {
                        TreeSelectionChanged(m_treeContext, EventArgs.Empty);
                    }
                }
                else if ((e.ChangeType & WatcherChangeTypes.Deleted) == WatcherChangeTypes.Deleted)
                {
                    m_treeControlAdapter.Refresh(m_treeContext.RootFolder);
                    if (parentDirName == selectedFolder)
                    {
                        TreeSelectionChanged(m_treeContext, EventArgs.Empty);
                    }
                    else if (itemName == selectedFolder)
                    {
                        m_treeContext.Clear();
                    }
                }
            }
            catch (System.IO.IOException)
            {
                // catch and suppress IO exceptions. This can occur often if (for example) we can't access the file because
                // of sharing priviledges
            }
        }

        private string GetNormalizedName(string path)
        {
            string result = string.IsNullOrWhiteSpace(path)
                ? string.Empty : path.ToLower().Replace('\\', '_').Replace('/', '_').TrimEnd('_');            
            return result;
        }

        private void ShutdownWatcher(object sender, EventArgs args)
        {
            if (m_watcher != null)
            {
                    // If we're going to call Dispose(), there may be no
                    // point in unbinding the events like this... But just
                    // for completeness, let's undo the startup operations.
                m_watcher.Renamed -= Watcher_Renamed;
                m_watcher.Deleted -= Watcher_FileChanged;
                m_watcher.Renamed -= Watcher_FileChanged;
                m_watcher.Dispose();
                m_watcher = null;
            }

            if (m_treeContext != null)
            {
                m_treeContext.Dispose();
                m_treeContext = null;
            }

            if (m_listContext != null)
            {
                m_listContext.Dispose();
                m_listContext = null;
            }
        }

        /// <summary>
        /// Gets Uri of last selected or null
        /// </summary>
        public ResourceDesc? LastSelected
        {
            get {
                object lastSelected = m_listContext.GetLastSelected<object>();
                if (lastSelected == null) return null;
                return m_resourceQueryService.GetDesc(lastSelected);
            }
        }

        public string ContextMenuTarget { get; private set; }

        /// <summary>
        /// Gets selected items</summary>
        public IEnumerable<object> Selection
        {
            get { return m_listContext.GetSelection<object>(); }
        }

        public interface ISubCommandClient : ICommandClient 
        {
            IEnumerable<object> GetCommands(string focusUri);
        }

        [ImportMany(typeof(ISubCommandClient))]
        private IEnumerable<ISubCommandClient> m_subCommands;

        protected virtual void OnSelectionChanged()
        {
            SelectionChanged(this, EventArgs.Empty);
        }

        #region IInitializable Members

        /// <summary>
        /// Initializes the MEF component</summary>
        public void Initialize()
        {
            // tree control
            m_treeControl = new TreeControl();
            m_treeControl.SelectionMode = SelectionMode.One;
            m_treeControl.Dock = DockStyle.Fill;
            m_treeControl.AllowDrop = true;            
            m_treeControl.ImageList = ResourceUtil.GetImageList16();
            m_treeControl.StateImageList = ResourceUtil.GetImageList16();
            m_treeControl.MouseUp += treeControl_MouseUp;
            m_treeControlAdapter = new TreeControlAdapter(m_treeControl);

            // list view
            m_listView = new CustomListView();
            m_listView.View = View.Details;
            m_listView.Dock = DockStyle.Fill;
            m_listView.AllowDrop = true;
            m_listView.LabelEdit = false;
            m_listView.MouseDown += ThumbnailControl_MouseDown;
            m_listView.MouseMove += ThumbnailControl_MouseMove;
            m_listView.MouseUp += ThumbnailControl_MouseUp;
            m_listView.MouseLeave += ThumbnailControl_MouseLeave;


            m_listViewAdapter = new ListViewAdapter(m_listView);

            // thumbnail control
            m_thumbnailControl = new ThumbnailControl();
            m_thumbnailControl.Dock = DockStyle.Fill;
            m_thumbnailControl.AllowDrop = true;
            m_thumbnailControl.BackColor = SystemColors.Window;
            m_thumbnailControl.Selection.Changed += thumbnailControl_SelectionChanged;
            m_thumbnailControl.MouseDown += ThumbnailControl_MouseDown;
            m_thumbnailControl.MouseMove += ThumbnailControl_MouseMove;
            m_thumbnailControl.MouseUp += ThumbnailControl_MouseUp;
            m_thumbnailControl.MouseLeave += ThumbnailControl_MouseLeave;


            // split
            m_splitContainer = new SplitContainer();
            m_splitContainer.Name = "Resources".Localize();
            m_splitContainer.Orientation = Orientation.Vertical;
            m_splitContainer.Panel1.Controls.Add(m_treeControl);
            m_splitContainer.Panel2.Controls.Add(m_thumbnailControl);
            m_splitContainer.Panel2.Controls.Add(m_listView);
            m_splitContainer.SplitterDistance = 10;            
            m_listView.Hide();

            // on initialization, register our tree control with the hosting service
            m_controlHostService.RegisterControl(
                m_splitContainer,
                new ControlInfo(
                   "Resources".Localize(),
                   "Lists available resources".Localize(),
                   StandardControlGroup.Left),
               this);


            m_thumbnailService.ThumbnailReady += ThumbnailManager_ThumbnailReady;
            Application.ApplicationExit += delegate
            {
                foreach (var item in m_thumbnailControl.Items)
                {
                    if (item.Image != null) item.Image.Dispose();
                }
                m_thumbnailControl.Items.Clear();
            };

            m_mainForm.Loaded += delegate
            {
                m_splitContainer.SplitterDistance = m_splitterDistance;
            };

            RegisterCommands();
            RegisterSettings();

                // We need to shut down the watcher before the dispose phase begins
                // this is because file events can occur while disposing other MEF objects
                // (eg, flushing out a cached file). When this happens, we can get a
                // callback that can end up accessing MEF objects that have already
                // been disposed.
                // Likewise we should dispose any IOpaqueResourceFolder because they can contain
                // native objects inside of them
            Application.ApplicationExit += ShutdownWatcher;
        }

        void ThumbnailControl_MouseLeave(object sender, EventArgs e)
        {
            m_dragging = false;
        }

        #endregion

        #region ICommandClient

        /// <summary>
        /// Checks whether the client can do the command if it handles it</summary>
        /// <param name="commandTag">Command to be done</param>
        /// <returns>True if client can do the command</returns>
        public bool CanDoCommand(object commandTag)
        {
            if (m_treeContext == null || m_treeContext.Root == null) // in case there is no currently opened document
                return false;

            if (commandTag is Command)
            {
                switch ((Command)commandTag)
                {
                    case Command.DetailsView:
                        return !m_listView.Visible;
                    case Command.ThumbnailView:
                        return !m_thumbnailControl.Visible;
                    default:
                        return false;
                }
            }

            foreach (var cc in m_subCommands)
                if (cc.CanDoCommand(commandTag))
                    return true;

            return false;
        }

        /// <summary>
        /// Does the command</summary>
        /// <param name="commandTag">Command to be done</param>
        public void DoCommand(object commandTag)
        {
            if (commandTag is Command)
            {
                switch ((Command)commandTag)
                {
                    case Command.DetailsView:
                        {
                            m_thumbnailControl.Hide();
                            m_listView.Show();
                            RefreshThumbnails();
                        }
                        break;

                    case Command.ThumbnailView:
                        {
                            m_listView.Hide();
                            m_thumbnailControl.Show();
                            RefreshThumbnails();
                        }
                        break;
                }
            }

            foreach (var cc in m_subCommands)
                cc.DoCommand(commandTag);
        }

        /// <summary>
        /// Updates command state for given command</summary>
        /// <param name="commandTag">Command</param>
        /// <param name="state">Command state to update</param>
        public void UpdateCommand(object commandTag, CommandState state)
        {
            foreach (var cc in m_subCommands)
                cc.UpdateCommand(commandTag, state);
        }

        #endregion

        #region IControlHostClient Members

        /// <summary>
        /// Notifies the client that its Control has been activated. Activation occurs when
        /// the Control gets focus, or a parent "host" Control gets focus.</summary>
        /// <param name="control">Client Control that was activated</param>
        /// <remarks>This method is only called by IControlHostService if the Control was previously
        /// registered by this IControlHostClient.</remarks>
        public void Activate(Control control)
        {
        }

        /// <summary>
        /// Notifies the client that its Control has been deactivated. Deactivation occurs when
        /// another Control or "host" Control gets focus.</summary>
        /// <param name="control">Client Control that was deactivated</param>
        /// <remarks>This method is only called by IControlHostService if the Control was previously
        /// registered by this IControlHostClient.</remarks>
        public void Deactivate(Control control)
        {
        }

        /// <summary>
        /// Requests permission to close the client's Control.</summary>
        /// <param name="control">Client Control to be closed</param>
        /// <returns>True if the Control can close, or false to cancel</returns>
        /// <remarks>
        /// * This method is only called by IControlHostService if the Control was previously
        /// registered by this IControlHostClient.
        /// * If true is returned, the IControlHostService calls its own
        /// UnregisterControl. The IControlHostClient has to call RegisterControl again
        /// if it wants to re-register this Control.</remarks>
        public bool Close(Control control)
        {
            return true;
        }

        #endregion

        private void TreeSelectionChanged(object sender, EventArgs e)
        {
            IOpaqueResourceFolder selectedFolder = m_treeContext.LastSelected.As<IOpaqueResourceFolder>();
            if (selectedFolder != null)
            {
                m_listContext.SelectedFolder = selectedFolder;
                RefreshThumbnails();
            }
        }

        private void treeControl_MouseUp(object sender, MouseEventArgs e)
        {
            if (e.Button == MouseButtons.Right)
            {
                Point clientPoint = new Point(e.X, e.Y);
                List<object> commands = new List<object>();
                commands.Add(Command.DetailsView);
                commands.Add(Command.ThumbnailView);

                Point screenPoint = m_treeControl.PointToScreen(clientPoint);
                m_commandService.RunContextMenu(commands, screenPoint);
            }
        }

        private void ThumbnailControl_MouseDown(object sender, MouseEventArgs e)
        {
            CustomListView listvew = sender as CustomListView;
            if (listvew != null)
            {
                listvew.MouseUpEventDisabled = false;
            }
            
            m_dragging = false;

            m_hitPoint = e.Location;
        }

        private void ThumbnailControl_MouseMove(object sender, MouseEventArgs e)
        {

            if (!m_dragging && e.Button == MouseButtons.Left)
            {
                Size dragSize = SystemInformation.DragSize;
                if (Math.Abs(m_hitPoint.X - e.X) >= dragSize.Width ||
                    Math.Abs(m_hitPoint.Y - e.Y) >= dragSize.Height)
                {
                    m_dragging = true;
                }
            }

            if (m_dragging)
            {
                string hitItem = GetClickedItemUri(m_hitPoint);
                if (hitItem != null)
                {
                    List<string> paths = new List<string>();

                    foreach (string uri in GetSelectedItemUris())
                    {
                        paths.Add(uri);
                    }

                    if (paths.Count > 0)
                    {
                        CustomListView listvew = sender as CustomListView;
                        if (listvew != null)
                        {
                            listvew.MouseUpEventDisabled = true;
                        }

                        Control ctrl = (Control)sender;
                        // Create the DataObject and fill it with an array of paths
                        // of the clicked and/or selected objects
                        IDataObject dataObject = new DataObject();
                        dataObject.SetData(DataFormats.FileDrop, true, paths.ToArray());
                        ctrl.DoDragDrop(dataObject, DragDropEffects.Move);
                    }
                }

            }
        }

        private void ThumbnailControl_MouseUp(object sender, MouseEventArgs e)
        {
            m_dragging = false;
            if (e.Button == MouseButtons.Right)
            {
                Point clientPoint = new Point(e.X, e.Y);
                ContextMenuTarget = GetClickedItemUri(clientPoint);

                List<object> commands = new List<object>(/*GetPopupCommandTags(target)*/);
                foreach (var cc in m_subCommands)
                    commands.AddRange(cc.GetCommands(ContextMenuTarget));
                commands.Add(Command.DetailsView);
                commands.Add(Command.ThumbnailView);

                Control ctrl = (Control)sender;
                Point screenPoint = ctrl.PointToScreen(clientPoint);
                m_commandService.RunContextMenu(commands, screenPoint);
            }
        }

        // Gets an enumeration of all Uri tags of items selected in the specified control
        // or an empty enumeration if no items are selected
        private IEnumerable<string> GetSelectedItemUris()
        {
            if (m_thumbnailControl.Visible)
            {
                foreach (ThumbnailControlItem item in m_thumbnailControl.Selection)
                {
                    yield return item.Uri.LocalPath;
                }
            }
            else
            {
                foreach (ListViewItem item in m_listView.SelectedItems)
                {
                    var desc = m_resourceQueryService.GetDesc(item.Tag);
                    if (desc.HasValue)
                        yield return desc.Value.MountedName;
                }
            }
        }

        private string GetClickedItemUri(Point point)
        {
            string resourceUri = null;
            if (m_thumbnailControl.Visible)
            {
                ThumbnailControlItem item = m_thumbnailControl.PickThumbnail(point);
                if (item != null)
                    resourceUri = item.Uri.LocalPath;
            }
            else
            {
                var desc = m_resourceQueryService.GetDesc(m_listViewAdapter.GetItemAt(point));
                resourceUri = desc.HasValue? desc.Value.MountedName : null;
            }

            return resourceUri;
        }

        private void listSelectionContext_SelectionChanged(object sender, EventArgs e)
        {
            OnSelectionChanged();
        }

        private void thumbnailControl_SelectionChanged(object sender, EventArgs e)
        {
            List<Uri> uris = new List<Uri>();
            foreach (var item in m_thumbnailControl.Selection)
                uris.Add(item.Uri);
            m_listContext.SetRange(uris);
        }

        private void ThumbnailManager_ThumbnailReady(object sender, ThumbnailReadyEventArgs e)
        {
            // get rid of temporary thumbnail
            Uri resourceUri = e.ResourceUri;
            ThumbnailControlItem item = GetItem(resourceUri);

            if (item != null)
            {
                if (item.Image != null) item.Image.Dispose();
                item.Image = e.Image;
            }
            m_thumbnailControl.Invalidate();
        }
      
        private void RefreshThumbnails()
        {
            foreach (var item in m_thumbnailControl.Items)
            {
                if (item.Image != null) item.Image.Dispose();
            }

            m_thumbnailControl.Selection.Clear();
            m_thumbnailControl.Items.Clear();
            m_thumbnailControl.Invalidate();
            if (m_treeContext != null)
            {
                IOpaqueResourceFolder currentAssetFolder = m_treeContext.GetLastSelected<IOpaqueResourceFolder>();
                if (currentAssetFolder == null || !m_thumbnailControl.Visible)
                    return;

                foreach (var resource in currentAssetFolder.Resources)
                {
                    Uri resourceUri = resource as Uri;
                    if (resourceUri == null || !resourceUri.IsAbsoluteUri) continue;   // (can't call LocalPath on a relative Uri)

                    m_thumbnailService.ResolveThumbnail(resourceUri);

                    Bitmap tempThumbnail;
                    string assetPath = resourceUri.LocalPath;
                    string assetFileName = Path.GetFileName(assetPath);
                    using (Icon shellIcon = FileIconUtil.GetFileIcon(assetFileName, FileIconUtil.IconSize.Large, false))
                        tempThumbnail = shellIcon.ToBitmap();

                    ThumbnailControlItem item = NewItem(resourceUri, tempThumbnail);
                    m_thumbnailControl.Items.Add(item);
                }
                m_thumbnailControl.RecalculateClientSize();
            }
        }

        private ThumbnailControlItem GetItem(Uri resourceUri)
        {
            foreach (ThumbnailControlItem item in m_thumbnailControl.Items)
            {
                if (item.Uri == resourceUri)
                    return item;
            }

            return null;
        }

        private ThumbnailControlItem NewItem(Uri resourceUri, Image image)
        {
            ThumbnailControlItem item = new ThumbnailControlItem(image, resourceUri);
            return item;
        }

        private void RegisterCommands()
        {
            m_commandService.RegisterCommand(
                Command.DetailsView,
                null,
                StandardCommandGroup.FileOther,
                "Details View".Localize(),
                "Switch to details view".Localize(),
                this);

            m_commandService.RegisterCommand(
                Command.ThumbnailView,
                null,
                StandardCommandGroup.FileOther,
                "Thumbnail View".Localize(),
                "Switch to thumbnail view".Localize(),
                this);
        }

        /// <summary>
        /// Gets or sets the currently selected asset list view mode. This string is
        /// persisted in the user's settings file.</summary>
        public string AssetListViewMode
        {
            get
            {
                if (m_thumbnailControl.Visible)
                    return "Thumbnail";
                else
                    return "Details";
            }

            set
            {
                // Make sure that 'value' is valid first, in case the names have changed
                //  or the settings file is otherwise invalid.
                if ("Details" == value)
                {
                    DoCommand(Command.DetailsView);
                }
                else if ("Thumbnail" == value)
                {
                    DoCommand(Command.ThumbnailView);
                }
            }
        }


        /// <summary>
        /// Gets or sets the settings string for the ListViewAdapter</summary>
        public string ListViewSettings
        {
            get { return m_listViewAdapter.Settings; }
            set { m_listViewAdapter.Settings = value; }
        }


        private int m_splitterDistance = 20;
        /// <summary>
        /// Gets or sets the ListView control bounds</summary>
        public int SplitterDistance
        {
            get { return m_splitContainer.SplitterDistance; }
            set
            {
                // delay setting it until form shown.
                m_splitterDistance = value;                
            }
        }

        public void Dispose() => Dispose(true);

        protected virtual void Dispose(bool disposing)
        {
            if (!disposing) return;
            if (m_splitContainer != null)
            {
                m_splitContainer.Dispose();
                m_splitContainer = null;
            }
            if (m_treeContext != null)
            {
                m_treeContext.Dispose();
                m_treeContext = null;
            }
            if (m_treeControl != null)
            {
                m_treeControl.Dispose();
                m_treeControl = null;
            }
            if (m_listContext != null)
            {
                m_listContext.Dispose();
                m_listContext = null;
            }
            if (m_listView != null)
            {
                m_listView.Dispose();
                m_listView = null;
            }
            if (m_thumbnailControl != null)
            {
                m_thumbnailControl.Dispose();
                m_thumbnailControl = null;
            }
            if (m_watcher != null)
            {
                m_watcher.Renamed -= Watcher_Renamed;
                m_watcher.Deleted -= Watcher_FileChanged;
                m_watcher.Renamed -= Watcher_FileChanged;
                m_watcher.Dispose();
                m_watcher = null;
            }
            Application.ApplicationExit -= ShutdownWatcher;
        }

        private void RegisterSettings()
        {
            m_settingsService.RegisterSettings(
                this,
                new BoundPropertyDescriptor(this, () => AssetListViewMode, "AssetListViewMode", null, null),
                new BoundPropertyDescriptor(this, () => SplitterDistance, "SplitterDistance", null, null),
                new BoundPropertyDescriptor(this, () => ListViewSettings, "ListViewSettings", null, null)
            );
        }

        private enum Command
        {
            DetailsView,
            ThumbnailView,
        }

        private SplitContainer m_splitContainer;

        // tree control
        private TreeViewContext m_treeContext;
        private TreeControlAdapter m_treeControlAdapter;
        private TreeControl m_treeControl;

        // list view
        private ListViewContext m_listContext;
        private ListView m_listView;
        private ListViewAdapter m_listViewAdapter;

        private ThumbnailControl m_thumbnailControl;

        private Point m_hitPoint;
        private bool m_dragging;

        [Import(AllowDefault = false)]
        private MainForm m_mainForm = null;

        [Import(AllowDefault = false)]
        private ICommandService m_commandService;

        [Import(AllowDefault = false)]
        private IControlHostService m_controlHostService;

        [Import(AllowDefault = false)]
        private ISettingsService m_settingsService;

        [Import(AllowDefault = false)]
        private ThumbnailService m_thumbnailService;

        [Import(AllowDefault = false)]
        private IResourceQueryService m_resourceQueryService;

        private SynchronizationContext m_uiThreadContext;

        private FileSystemWatcher m_watcher;

        #region private classes
        private class TreeViewContext : ITreeView, IItemView, IObservableContext, ISelectionContext, IDisposable
        {
            public TreeViewContext(IOpaqueResourceFolder rootFolder)
            {
                m_rootFolder = rootFolder;
                m_selection.Changing += TheSelectionChanging;
                m_selection.Changed += TheSelectionChanged;

            }

            public void Reload()
            {
                OnReloaded(EventArgs.Empty);
            }

            #region ISelectionContext Members

            /// <summary>
            /// Gets or sets the selected items</summary>
            public IEnumerable<object> Selection
            {
                get { return m_selection; }
                set { m_selection.SetRange(value); }
            }

            /// <summary>
            /// Gets all selected items of the given type</summary>
            /// <typeparam name="T">Desired item type</typeparam>
            /// <returns>All selected items of the given type</returns>
            public IEnumerable<T> GetSelection<T>() where T : class
            {
                return m_selection.AsIEnumerable<T>();
            }

            /// <summary>
            /// Gets the last selected item as object</summary>
            public object LastSelected
            {
                get { return m_selection.LastSelected; }
            }

            /// <summary>
            /// Gets the last selected item of the given type, which may not be the same
            /// as the LastSelected item</summary>
            /// <typeparam name="T">Desired item type</typeparam>
            /// <returns>Last selected item of the given type</returns>
            public T GetLastSelected<T>() where T : class
            {
                return m_selection.GetLastSelected<T>();
            }

            /// <summary>
            /// Returns whether the selection contains the given item</summary>
            /// <param name="item">Item</param>
            /// <returns>True iff the selection contains the given item</returns>
            public bool SelectionContains(object item)
            {
                return m_selection.Contains(item);
            }

            /// <summary>
            /// Gets the number of items in the current selection</summary>
            public int SelectionCount
            {
                get { return m_selection.Count; }
            }

            /// <summary>
            /// Event that is raised before the selection changes</summary>
            public event EventHandler SelectionChanging;

            /// <summary>
            /// Event that is raised after the selection changes</summary>
            public event EventHandler SelectionChanged;

            #endregion

            #region ITreeView Members

            public object Root
            {
                get { return m_rootFolder; }
            }

            public IEnumerable<object> GetChildren(object parent)
            {
                IOpaqueResourceFolder resourceFolder = parent.As<IOpaqueResourceFolder>();
                if (resourceFolder != null)
                {
                    foreach (IOpaqueResourceFolder childFolder in resourceFolder.Subfolders)
                        yield return childFolder;
                }
            }

            #endregion

            #region IItemView Members

            /// <summary>
            /// Gets item's display information</summary>
            /// <param name="item">Item being displayed</param>
            /// <param name="info">Item info, to fill out</param>
            public virtual void GetInfo(object item, ItemInfo info)
            {
                IOpaqueResourceFolder resourceFolder = item.As<IOpaqueResourceFolder>();
                if (resourceFolder != null)
                {
                    info.Label = resourceFolder.Name;
                    info.ImageIndex = info.GetImageList().Images.IndexOfKey(Sce.Atf.Resources.FolderImage);
                    info.AllowLabelEdit = false; //  !resourceFolder.ReadOnlyName;
                    info.IsLeaf = resourceFolder.IsLeaf;
                }
            }

            #endregion

            #region IObservableContext Members

            public event EventHandler<ItemInsertedEventArgs<object>> ItemInserted;

            public event EventHandler<ItemRemovedEventArgs<object>> ItemRemoved;

            public event EventHandler<ItemChangedEventArgs<object>> ItemChanged;

            public event EventHandler Reloaded;

            protected virtual void OnItemInserted(ItemInsertedEventArgs<object> e)
            {
                if (ItemInserted != null)
                    ItemInserted(this, e);
            }

            protected virtual void OnItemRemoved(ItemRemovedEventArgs<object> e)
            {
                if (ItemRemoved != null)
                    ItemRemoved(this, e);
            }

            protected virtual void OnItemChanged(ItemChangedEventArgs<object> e)
            {
                if (ItemChanged != null)
                    ItemChanged(this, e);
            }

            protected virtual void OnReloaded(EventArgs e)
            {
                if (Reloaded != null)
                {
                    Reloaded(this, e);
                    this.Clear();
                }
            }

            #endregion

            public IOpaqueResourceFolder RootFolder
            {
                get { return m_rootFolder; }
            }

            public void Dispose() => Dispose(true);

            protected virtual void Dispose(bool disposing)
            {
                if (!disposing) return;
                IDisposable disposableRootFolder = m_rootFolder as IDisposable; 
                if (disposableRootFolder != null)
                {
                    disposableRootFolder.Dispose();
                }
                m_rootFolder = null;
                m_selection = null;
            }

            private void TheSelectionChanging(object sender, EventArgs e)
            {
                SelectionChanging.Raise(this, EventArgs.Empty);
            }

            private void TheSelectionChanged(object sender, EventArgs e)
            {
                SelectionChanged.Raise(this, EventArgs.Empty);
            }

            private AdaptableSelection<object> m_selection = new AdaptableSelection<object>();
            private IOpaqueResourceFolder m_rootFolder;
        }
        private class ListViewContext : IListView, IItemView, IObservableContext, ISelectionContext, IDisposable
        {
            public ListViewContext()
            {
                m_selection.Changing += TheSelectionChanging;
                m_selection.Changed += TheSelectionChanged;
            }

            internal IResourceQueryService ResourceQueryService { get; set; }

            #region IListView Members

            /// <summary>
            /// Gets names for table columns</summary>
            public string[] ColumnNames
            {
                get { return s_columnNames; }
            }

            private static readonly string[] s_columnNames = new[]
            {
                "Name", "Size", "Type", "Date Modified"
            };

            public IEnumerable<object> Items
            {
                get
                {
                    IOpaqueResourceFolder resourceFolder = SelectedFolder;
                    if (resourceFolder != null)
                    {
                        foreach (object resource in resourceFolder.Resources)
                            yield return resource;
                    }
                }
            }

            #endregion

            #region IItemView Members

            /// <summary>
            /// Gets item's display information</summary>
            /// <param name="item">Item being displayed</param>
            /// <param name="info">Item info, to fill out</param>
            public virtual void GetInfo(object item, ItemInfo info)
            {
                var optDesc = ResourceQueryService.GetDesc(item);
                if (optDesc.HasValue)
                {
                    var desc = optDesc.Value;
                    info.Label = desc.ShortName;
                    info.ImageIndex = info.GetImageList().Images.IndexOfKey(Sce.Atf.Resources.ResourceImage);
                    info.Properties = new object[] {
                        desc.SizeInBytes,
                        desc.Types.ToString(),
                        desc.ModificationTime
                    };
                }
                else
                {
                    info.Label = "<could not get info>";
                }
            }
            #endregion

            #region IObservableContext Members

            public event EventHandler<ItemInsertedEventArgs<object>> ItemInserted;

            public event EventHandler<ItemRemovedEventArgs<object>> ItemRemoved;

            public event EventHandler<ItemChangedEventArgs<object>> ItemChanged;

            public event EventHandler Reloaded;

            protected virtual void OnItemInserted(ItemInsertedEventArgs<object> e)
            {
                if (ItemInserted != null)
                    ItemInserted(this, e);
            }

            protected virtual void OnItemRemoved(ItemRemovedEventArgs<object> e)
            {
                if (ItemRemoved != null)
                    ItemRemoved(this, e);
            }

            protected virtual void OnItemChanged(ItemChangedEventArgs<object> e)
            {
                if (ItemChanged != null)
                    ItemChanged(this, e);
            }

            protected virtual void OnReloaded(EventArgs e)
            {
                if (Reloaded != null)
                    Reloaded(this, e);
            }

            #endregion

            #region ISelectionContext Members

            /// <summary>
            /// Gets or sets the selected items</summary>
            public IEnumerable<object> Selection
            {
                get { return m_selection; }
                set { m_selection.SetRange(value); }
            }

            /// <summary>
            /// Gets all selected items of the given type</summary>
            /// <typeparam name="T">Desired item type</typeparam>
            /// <returns>All selected items of the given type</returns>
            public IEnumerable<T> GetSelection<T>() where T : class
            {
                return m_selection.AsIEnumerable<T>();
            }

            /// <summary>
            /// Gets the last selected item as object</summary>
            public object LastSelected
            {
                get { return m_selection.LastSelected; }
            }

            /// <summary>
            /// Gets the last selected item of the given type, which may not be the same
            /// as the LastSelected item</summary>
            /// <typeparam name="T">Desired item type</typeparam>
            /// <returns>Last selected item of the given type</returns>
            public T GetLastSelected<T>() where T : class
            {
                return m_selection.GetLastSelected<T>();
            }

            /// <summary>
            /// Returns whether the selection contains the given item</summary>
            /// <param name="item">Item</param>
            /// <returns>True iff the selection contains the given item</returns>
            public bool SelectionContains(object item)
            {
                return m_selection.Contains(item);
            }

            /// <summary>
            /// Gets the number of items in the current selection</summary>
            public int SelectionCount
            {
                get { return m_selection.Count; }
            }

            /// <summary>
            /// Event that is raised before the selection changes</summary>
            public event EventHandler SelectionChanging;

            /// <summary>
            /// Event that is raised after the selection changes</summary>
            public event EventHandler SelectionChanged;

            #endregion

            public IOpaqueResourceFolder SelectedFolder
            {
                get { return m_selectedFolder; }
                set
                {
                    m_selectedFolder = value;
                    this.Clear();
                    OnReloaded(EventArgs.Empty);
                }
            }

            public void Dispose() => Dispose(true);

            protected virtual void Dispose(bool disposing)
            {
                if (!disposing) return;
                IDisposable disposableSelectedFolder = m_selectedFolder as IDisposable;
                if (disposableSelectedFolder != null)
                {
                    disposableSelectedFolder.Dispose();
                }
                m_selectedFolder = null;
                m_selection = null;
            }

            private void TheSelectionChanging(object sender, EventArgs e)
            {
                SelectionChanging.Raise(this, EventArgs.Empty);
            }

            private void TheSelectionChanged(object sender, EventArgs e)
            {
                SelectionChanged.Raise(this, EventArgs.Empty);
            }

            private IOpaqueResourceFolder m_selectedFolder;
            private AdaptableSelection<object> m_selection = new AdaptableSelection<object>();
        }
        private class CustomListView : ListView
        {
            public bool MouseUpEventDisabled
            {
                get;
                set;
            }


            protected override void OnMouseUp(MouseEventArgs e)
            {
                if (MouseUpEventDisabled) return;
                base.OnMouseUp(e);
            }
        }
    
        #endregion
    }
}

