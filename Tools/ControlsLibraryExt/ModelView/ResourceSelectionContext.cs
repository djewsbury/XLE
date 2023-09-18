using Sce.Atf.Applications;
using Sce.Atf;
using System;
using System.Collections.Generic;
using Sce.Atf.Adaptation;

namespace ControlsLibraryExt.ModelView
{
    public class ResourceSelectionTreeViewContext : ITreeView, ITreeListView, IItemView, IObservableContext, ISelectionContext, IDisposable
    {
        public ResourceSelectionTreeViewContext(LevelEditorCore.IOpaqueResourceFolder rootFolder, LevelEditorCore.IResourceQueryService resourceQuery)
        {
            m_rootFolder = rootFolder;
            _resourceQuery = resourceQuery;
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
            LevelEditorCore.IOpaqueResourceFolder resourceFolder = parent.As<LevelEditorCore.IOpaqueResourceFolder>();
            if (resourceFolder != null)
            {
                foreach (LevelEditorCore.IOpaqueResourceFolder childFolder in resourceFolder.Subfolders)
                    yield return childFolder;

                foreach (object resource in resourceFolder.Resources)
                    yield return resource;
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
            LevelEditorCore.IOpaqueResourceFolder resourceFolder = item.As<LevelEditorCore.IOpaqueResourceFolder>();
            if (resourceFolder != null)
            {
                info.Label = resourceFolder.Name;
                info.ImageIndex = info.GetImageList().Images.IndexOfKey(Sce.Atf.Resources.FolderImage);
                info.AllowLabelEdit = false;
                info.IsLeaf = false; // resourceFolder.IsLeaf; (never a leaf, because we interleave files)
            }
            else 
            {
                info.IsLeaf = true;
                info.AllowLabelEdit = false;

                if (_resourceQuery != null)
                {
                    var desc = _resourceQuery.GetDesc(item);
                    if (desc.HasValue)
                    {
                        info.Label = desc?.ShortName;
                        if ((desc.Value.Types & (uint)LevelEditorCore.ResourceTypeFlags.Model) != 0)
                        {
                            info.Properties = new object[] { "Model Type", desc?.Filesystem, desc?.NaturalName };
                            info.ImageIndex = info.GetImageList().Images.IndexOfKey(Sce.Atf.Resources.PackageImage);
                        }
                        else
                            info.ImageIndex = info.GetImageList().Images.IndexOfKey(Sce.Atf.Resources.DocumentUnknownImage);
                        return;
                    }
                }

                // fallback to something generic
                info.Label = "Resource";
            }
        }

        #endregion

        #region ITreeListView Members
        public IEnumerable<object> Roots { get { yield return m_rootFolder; } }

        public string[] ColumnNames { get { string[] result = { "Name", "Desc", "Filesystem", "Natural Name" }; return result; } }
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

        public LevelEditorCore.IOpaqueResourceFolder RootFolder
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
        private LevelEditorCore.IOpaqueResourceFolder m_rootFolder;
        private LevelEditorCore.IResourceQueryService _resourceQuery;
    }
}
