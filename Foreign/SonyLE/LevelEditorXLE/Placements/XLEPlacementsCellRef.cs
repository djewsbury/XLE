// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

using System;
using System.ComponentModel.Composition;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;

using Sce.Atf;
using Sce.Atf.Adaptation;
using Sce.Atf.Applications;
using Sce.Atf.Dom;
using Sce.Atf.VectorMath;

using LevelEditorCore;
using LevelEditorCore.GenericAdapters.Extensions;

using XLEBridgeUtils;
using LevelEditorXLE.Extensions;

namespace LevelEditorXLE.Placements
{
    using CellRefST = Schema.placementsCellReferenceType;
    using FolderST = Schema.placementsFolderType;

    public class PlacementsCellRef
        : LevelEditorCore.GenericAdapters.GenericReference<XLEPlacementDocument>
        , IHierarchical, IExportable, IEnumerableContext
    {
        public Vec3F CaptureMins
        {
            get { return this.GetVec3(CellRefST.captureMinsAttribute); }
            set { this.SetVec3(CellRefST.captureMinsAttribute, value); }
        }

        public Vec3F CaptureMaxs
        {
            get { return this.GetVec3(CellRefST.captureMaxsAttribute); }
            set { this.SetVec3(CellRefST.captureMaxsAttribute, value); }
        }

        public Vec3F Offset
        {
            get { return this.GetVec3(CellRefST.offsetAttribute); }
            set { this.SetVec3(CellRefST.offsetAttribute, value); }
        }

        public string Name
        {
            get { return GetAttribute<string>(CellRefST.nameAttribute); }
            set { SetAttribute(CellRefST.nameAttribute, value); }
        }

        public Tuple<Vec3F, Vec3F> CalculateCellBoundary()
        {
            if (IsResolved())
            {
                    // if we are resolved, we can get the cell boundary from the
                    // native stuff
                var target = Target.As<XLEBridgeUtils.INativeDocumentAdapter>();
                if (target != null)
                {
                    var sceneMan = Target.GetSceneManager();
                    var boundary = GUILayer.EditorInterfaceUtils.CalculatePlacementCellBoundary(
                        sceneMan, target.NativeDocumentId);
                    var result = Tuple.Create(
                        XLEBridgeUtils.Utils.AsVec3F(boundary.Item1),
                        XLEBridgeUtils.Utils.AsVec3F(boundary.Item2));
                    this.SetVec3(CellRefST.cachedCellMinsAttribute, result.Item1);
                    this.SetVec3(CellRefST.cachedCellMaxsAttribute, result.Item2);
                    return result;
                }
            }

            return Tuple.Create(
                this.GetVec3(CellRefST.cachedCellMinsAttribute),
                this.GetVec3(CellRefST.cachedCellMaxsAttribute));
        }

        #region IExportable
        public Uri ExportTarget
        {
            get 
            {
                var root = DomNode.GetRoot();
                var gameExt = root.As<LevelEditorXLE.Game.GameExtensions>();
                if (gameExt != null)
                    return new Uri(gameExt.ExportDirectory, Name + ".plc");
                return new Uri(Name + ".plc");
            }
        }

        public string ExportCategory
        {
            get { return "Placements"; }
        }

        private class ErrorExport : GUILayer.EditorSceneManager.PendingExport
        {
            public override GUILayer.EditorSceneManager.ExportResult PerformExport(Uri destFile)
            {
                throw new NotImplementedException("Attempting to perform impossible export");
            }
        };

        public IEnumerable<PendingExport> BuildPendingExports()
        {
            if (!IsResolved())
                return new List<PendingExport>{
                    new PendingExport(
                        ExportTarget,
                        new ErrorExport
                            { _success = false, _messages = "Cell reference is unresolved" })};

            var target = Target.As<XLEBridgeUtils.INativeDocumentAdapter>();
            if (target == null)
                return new List<PendingExport>{
                    new PendingExport(
                        ExportTarget,
                        new ErrorExport
                            { _success = false, _messages = "Error resolving target" })};

            var sceneMan = Target.GetSceneManager();
            var result = new List<PendingExport>();
            result.Add(new PendingExport(ExportTarget, sceneMan.ExportPlacements(target.NativeDocumentId)));
            return result;
        }
        #endregion
        #region IHierachical Members
        public bool CanAddChild(object child) 
        {
            if (m_target == null) return false;
            var hierarchical = m_target.AsAll<IHierarchical>();
            foreach (var h in hierarchical)
                if (h.CanAddChild(child)) 
                    return true;
            return false;
        }
        public bool AddChild(object child) 
        {
            if (m_target == null) return false;
            var hierarchical = m_target.AsAll<IHierarchical>();
            foreach (var h in hierarchical)
                if (h.AddChild(child))
                    return true;
            return false;
        }
        #endregion
        #region GenericReference<> Members
        static PlacementsCellRef()
        {
            s_nameAttribute = CellRefST.nameAttribute;
            s_refAttribute = CellRefST.refAttribute;
        }

        protected override XLEPlacementDocument Attach(Uri uri)
        {
            if (uri==null) return XLEPlacementDocument.OpenUntitled();
            return XLEPlacementDocument.OpenOrCreate(uri, Globals.MEFContainer.GetExportedValue<ISchemaLoader>());
        }

        protected override void Detach(XLEPlacementDocument target)
        {
            XLEPlacementDocument.Release(target);
        }
        #endregion
        #region IResolveable
        public override bool CanCreateNew() { return true; }
        public override void CreateAndResolve()
        {
                // Create a new placements document on the place we're we are expecting
                // it, and then attempt to resolve it. We just need to force an attach,
                // this will end up calling XLEPlacementDocument.OpenOrCreate()
            try
            {
                m_target = Attach(Uri);
            }
            catch (Exception e) { m_error = "While attempting to create new file: " + e.Message; }
        }

        public override bool CanSave() { return IsResolved() && Target.Dirty; }
        public override void Save(ISchemaLoader schemaLoader)
        {
            SelectNameIfNecessary();
            Target.Save(Uri, schemaLoader);
        }
        #endregion
        #region IEnumerableContext
        public IEnumerable<object> Items
        {
            get
            {
                var t = Target;
                if (t != null) return t.Items;
                return System.Linq.Enumerable.Empty<object>();
            }
        }
        #endregion

        public void SelectNameIfNecessary()
        {
            if (Uri == null)
            {
                System.Windows.Forms.MessageBox.Show(
                    "Select a name for the placements document", "Level Editor",
                    System.Windows.Forms.MessageBoxButtons.OK);

                var dlg = new System.Windows.Forms.SaveFileDialog();
                dlg.Filter = "Placement documents (*.plcdoc)|*.plcdoc";
                if (dlg.ShowDialog() != System.Windows.Forms.DialogResult.OK) return;

                var root = DomNode.GetRoot();
                var doc = root.As<IDocument>();
                Uri = new Uri(dlg.FileName);
                Target.Dirty = true;
            }
        }

        public static DomNode Create(Uri reference, string name, Vec3F mins, Vec3F maxs)
        {
            var result = new DomNode(CellRefST.Type);
            result.SetAttribute(CellRefST.refAttribute, reference);
            result.SetAttribute(CellRefST.nameAttribute, name);
            DomNodeUtil.SetVector(result, CellRefST.captureMinsAttribute, mins);
            DomNodeUtil.SetVector(result, CellRefST.captureMaxsAttribute, maxs);
            return result;
        }
    }

    public class PlacementsFolder : DomNodeAdapter, IListable, ICommandClient, IContextMenuCommandProvider, IExportable, IHierarchical
    {
        public static DomNode Create()
        {
            return new DomNode(FolderST.Type);
        }

        public static DomNode CreateWithConfigure()
        {
            var result = new DomNode(FolderST.Type);
            var adapter = result.As<PlacementsFolder>();
            if (adapter != null && adapter.DoModalConfigure())
                return result;
            return null;
        }

        public static DomNode CreateStarter()
        {
            var result = new DomNode(FolderST.Type);
            var adapter = result.As<PlacementsFolder>();
            if (adapter != null)
            {
                    // initialize to the default settings
                var cfg = adapter.GetCfg();
                cfg.UnnamedPlacementDocuments = true;
                adapter.Reconfigure(cfg);

                    // add a simple placement with a default object

                var defResource = Globals.ResourceService.Load(new Uri("xleres/DefaultResources/materialsphere.dae", UriKind.Relative));
                if (defResource != null)
                {
                    var plcmnt = XLEPlacementObject.Create(defResource);
                    if (plcmnt != null)
                    {
                        plcmnt.DomNode.InitializeExtensions();
                        adapter.AddChild(plcmnt);
                    }
                }
                return result;
            }
            return null;
        }

        public void GetInfo(ItemInfo info)
        {
            info.ImageIndex = Util.GetTypeImageIndex(DomNode.Type, info.GetImageList());
            info.Label = "Placements";
        }

        public System.Collections.Generic.IEnumerable<PlacementsCellRef> Cells
        {
            get { return GetChildList<PlacementsCellRef>(FolderST.cellChild); }
        }

        internal Uri BaseEditorPath
        {
            get { return GetAttribute<Uri>(FolderST.baseEditorPathAttribute); }
            set { SetAttribute(FolderST.baseEditorPathAttribute, value); }
        }

        internal int[] CellCount
        {
            get { return GetAttribute<int[]>(FolderST.CellCountAttribute); }
        }

        internal float[] CellsOrigin
        {
            get { return GetAttribute<float[]>(FolderST.CellsOriginAttribute); }
        }

        static private string FixPath(string input)
        {
            input.Replace('\\', '/');
            if (input.Length != 0 && input[input.Length - 1] != '/')
                input += "/";
            return input;
        }

        #region Configure Steps
        internal Uri GetBaseUri()
        {
            var root = DomNode.GetRoot().As<IResource>();
            if (root != null) return root.Uri;
            return new Uri(Directory.GetCurrentDirectory().TrimEnd('\\') + "\\");
        }

        internal void Reconfigure(PlacementsConfig.Config cfg)
        {
                // Based on the configuration settings given, remove and attach
                // cell references. First create a list of all of the references
                // we want to see.
                // We can use this to filter out the existing references that should
                // be removed. 
                // Then, we should create new references for all new cells that have
                // been added.
                // Cells always have the same basic filename structure, and are
                // arranged in a dense 2d grid. The filenames contain the x and y
                // coordinates of the cell in that grid.

            BaseEditorPath = new Uri(GetBaseUri(), cfg.BaseEditorPath);
            SetAttribute(FolderST.CellCountAttribute, new int[2] { (int)cfg.CellCountX, (int)cfg.CellCountY } );
            SetAttribute(FolderST.CellSizeAttribute, cfg.CellSize);
            SetAttribute(FolderST.CellsOriginAttribute, new float[2] { cfg.CellsOriginX, cfg.CellsOriginY } );

            var newCells = new List<Tuple<Uri, string, Vec3F, Vec3F>>();
            var cellCount = CellCount;
            for (uint y=0; y<cellCount[1]; ++y)
                for (uint x = 0; x < cellCount[0]; ++x) {
                    var rf = String.Format("p{0,3:D3}_{1,3:D3}.plcdoc", x, y);
                    var name = String.Format("{0,2:D2}-{1,2:D2}", x, y);
                    var mins = new Vec3F(x * cfg.CellSize + cfg.CellsOriginX, y * cfg.CellSize + cfg.CellsOriginY, -5000.0f);
                    var maxs = new Vec3F((x+1) * cfg.CellSize + cfg.CellsOriginX, (y+1) * cfg.CellSize + cfg.CellsOriginY, 5000.0f);
                    newCells.Add(Tuple.Create(new Uri(BaseEditorPath, rf), name, mins, maxs));
                }

            var foundMatching = new bool[newCells.Count];
            Array.Clear(foundMatching, 0, foundMatching.Length);

            var toBeRemoved = new List<PlacementsCellRef>();
            var existingRefs = Cells;
            foreach(var r in existingRefs) {
                var rawRef = r.Uri;
                int index = newCells.FindIndex(0, newCells.Count, s => s.Item1.Equals(rawRef));
                if (index >= 0) {
                    foundMatching[index] = true;
                    r.Name = newCells[index].Item2;
                    r.CaptureMins = newCells[index].Item3;
                    r.CaptureMaxs = newCells[index].Item4;
                } else
                    toBeRemoved.Add(r);
            }

                // \todo -- if we have some cell references to be removed, we should first check if there
                // are any changes, and if they need to be saved!
            foreach (var r in toBeRemoved)
            {
                r.Unresolve();
                r.DomNode.RemoveFromParent();
            }

                // Create the new cell references that are needed...
            var childList = DomNode.GetChildList(Schema.placementsFolderType.cellChild);
            for (int c = 0; c < newCells.Count; ++c)
                if (!foundMatching[c])
                {
                    var newItem = PlacementsCellRef.Create(
                        cfg.UnnamedPlacementDocuments ? null : newCells[c].Item1, 
                        newCells[c].Item2, newCells[c].Item3, newCells[c].Item4);
                    childList.Add(newItem);
                    newItem.As<PlacementsCellRef>().CreateAndResolve();
                }
        }

        internal PlacementsConfig.Config GetCfg()
        {
            var cfg = new PlacementsConfig.Config();
            cfg.BaseEditorPath = (BaseEditorPath != null) ? GetBaseUri().MakeRelativeUri(BaseEditorPath).ToString() : "";
            var cellCount = CellCount;
            if (cellCount[0] > 0 && cellCount[1] > 0)
            {
                cfg.CellCountX = (uint)cellCount[0];
                cfg.CellCountY = (uint)cellCount[1];
            }
            var cellsOrigin = CellsOrigin;
            cfg.CellsOriginX = cellsOrigin[0];
            cfg.CellsOriginY = cellsOrigin[1];
            cfg.CellSize = GetAttribute<float>(FolderST.CellSizeAttribute);
            return cfg;
        }

        internal bool DoModalConfigure()
        {
                // open the configuration dialog
            var cfg = GetCfg();
            using (var dlg = new PlacementsConfig())
            {
                dlg.Value = cfg;
                var result = dlg.ShowDialog();
                if (result == System.Windows.Forms.DialogResult.OK)
                {
                    Reconfigure(cfg);
                    return true;
                }
            }
            return false;
        }
        #endregion

        #region ICommandClient Members
        bool ICommandClient.CanDoCommand(object commandTag)
        {
            return commandTag is Command;
        }

        void ICommandClient.DoCommand(object commandTag)
        {
            if (!(commandTag is Command)) return;

            switch ((Command)commandTag)
            {
            case Command.Configure:
                {
                    DoModalConfigure();
                    break;
                }
            }
        }

        void ICommandClient.UpdateCommand(object commandTag, CommandState commandState)
        {}
        #endregion

        #region IExportable
        public Uri ExportTarget
        {
            get 
            {
                var root = DomNode.GetRoot();
                var gameExt = root.As<LevelEditorXLE.Game.GameExtensions>();
                if (gameExt != null)
                    return new Uri(gameExt.ExportDirectory, "placements.cfg");
                return new Uri("placements.cfg");
            }
        }

        public string ExportCategory { get { return "Placements"; } }

        public IEnumerable<PendingExport> BuildPendingExports()
        {
            var result = new List<PendingExport>();
            var e = this.GetSceneManager().ExportPlacementsCfg(BuildExportRefs());
            result.Add(new PendingExport(ExportTarget, e));
            return result;
        }

        private IEnumerable<GUILayer.EditorSceneManager.PlacementCellRef> BuildExportRefs()
        {
            var refs = new List<GUILayer.EditorSceneManager.PlacementCellRef>();
            foreach (var c in Cells)
            {
                var boundary = c.CalculateCellBoundary();
                var resService = Globals.MEFContainer.GetExportedValue<IXLEAssetService>();
                refs.Add(new GUILayer.EditorSceneManager.PlacementCellRef
                    {
                        NativeFile = resService.AsAssetName(c.ExportTarget),
                        Offset = XLEBridgeUtils.Utils.AsVector3(c.Offset),
                        Mins = XLEBridgeUtils.Utils.AsVector3(boundary.Item1),
                        Maxs = XLEBridgeUtils.Utils.AsVector3(boundary.Item2)
                    });
            }
            return refs;
        }
        #endregion

        #region IHierachical Members
        public bool CanAddChild(object child) 
        {
            // todo -- we could check which placement document is most appropriate for this!
            foreach (var c in Cells)
                if (c.CanAddChild(child))
                    return true;
            return false;
        }
        public bool AddChild(object child) 
        {
            // We need to look for the appropriate placement cell to put this placement in...
            // If there are no placement cells, or if there isn't a cell that can contain this
            // object, then we have to abort
            var plc = child.As<ITransformable>();
            if (plc == null) return false;

            var keyPoint = (plc != null) ? plc.Translation : new Sce.Atf.VectorMath.Vec3F(0.0f, 0.0f, 0.0f);

            foreach (var cellRef in Cells)
            {
                if (cellRef.IsResolved()
                    && keyPoint.X >= cellRef.CaptureMins.X && keyPoint.X < cellRef.CaptureMaxs.X
                    && keyPoint.Y >= cellRef.CaptureMins.Y && keyPoint.Y < cellRef.CaptureMaxs.Y
                    && keyPoint.Z >= cellRef.CaptureMins.Z && keyPoint.Z < cellRef.CaptureMaxs.Z)
                {
                    if (cellRef.AddChild(child))
                        return true;
                }
            }
            return false;
        }
        #endregion

        private enum Command
        {
            [Description("Configure Placements...")]
            Configure
        }

        IEnumerable<object> IContextMenuCommandProvider.GetCommands(object context, object target)
        {
            foreach (Command command in Enum.GetValues(typeof(Command)))
            {
                yield return command;
            }
        }
    }
}

