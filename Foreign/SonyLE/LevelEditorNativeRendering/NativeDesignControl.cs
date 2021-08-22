//Copyright © 2014 Sony Computer Entertainment America LLC. See License.txt.

using System;
using System.Windows.Forms;
using System.Drawing;
using System.Collections.Generic;
using System.ComponentModel.Composition;
using System.Linq;
using LevelEditorCore.VectorMath;
using Sce.Atf;
using Sce.Atf.Adaptation;
using Sce.Atf.Applications;
using Sce.Atf.Dom;
using Sce.Atf.VectorMath;

using LevelEditorCore;
using XLEBridgeUtils;

using ViewTypes = Sce.Atf.Rendering.ViewTypes;

namespace RenderingInterop
{
    [PartCreationPolicy(CreationPolicy.Shared)]
    public class NativeDesignControl : DesignViewControl, GUILayer.IViewContext
    {
        public NativeDesignControl(DesignView designView) :
            base(designView)
        {
            if (s_marqueePen == null)
            {
                s_marqueePen = new Pen(Color.FromArgb(30, 30, 30), 2);
                s_marqueePen.DashPattern = new float[] { 3, 3 };
            }

            m_renderState = new RenderState();
            m_renderState.RenderFlag = GlobalRenderFlags.Solid | GlobalRenderFlags.Textured | GlobalRenderFlags.Lit | GlobalRenderFlags.Shadows;
            m_renderState.WireFrameColor = Color.DarkBlue;
            m_renderState.SelectionColor = Color.FromArgb(66, 255, 161);
            BackColor = SystemColors.ControlDark;
            m_renderState.OnChanged += (sender, e) => Invalidate();

            Adapter = new DesignControlAdapter(
                this, Camera, 
                GameEngine.GetEditorSceneManager(), 
                GameEngine.GlobalSelection, GameEngine.GetSavedResources());

            Adapter.AddRenderCallback((GUILayer.SimpleRenderingContext context) => RenderManipulators(context, designView));
            Adapter.AddRenderCallback((GUILayer.SimpleRenderingContext context) => RenderExtras(context, designView));
            Adapter.AddNonRenderPassCallback((GUILayer.SimpleRenderingContext context) => RenderManipulatorsPostProcess(context, designView));
        }

        public ulong SurfaceId
        {
            get;
            private set;
        }

        public RenderState RenderState
        {
            get { return m_renderState; }
        }

        public DesignControlAdapter Adapter;

        protected override void Dispose(bool disposing)
        {
            if (disposing)
            {
                m_renderState.Dispose();
            }
            base.Dispose(disposing);
        }
        
        protected override IEnumerable<object> Pick(MouseEventArgs e)
        {           
            bool multiSelect = DragOverThreshold;
            Picking.HitRecord[] hits;
            if(multiSelect)
            {
                RectangleF rect = MakeRect(FirstMousePoint, CurrentMousePoint);
                var frustum = XLEBridgeUtils.Utils.MakeFrustumMatrix(Utils.AsCameraDesc(Camera), rect, ClientSize);
                hits = Picking.FrustumPick(
                    GameEngine.GetEngineDevice(),
                    Adapter.SceneManager,
                    frustum, Utils.AsCameraDesc(Camera), ClientSize, 
                    Picking.Flags.Objects | Picking.Flags.Helpers);
            }
            else
            {
                Ray3F rayW = GetWorldRay(CurrentMousePoint);
                hits = Picking.RayPick(
                    GameEngine.GetEngineDevice(),
                    Adapter.SceneManager,
                    rayW, Utils.AsCameraDesc(Camera), ClientSize,
                    Picking.Flags.Terrain | Picking.Flags.Objects | Picking.Flags.Helpers);
            }

            if (hits==null) return new List<object>();

            // create unique list of hits
            var uniqueHits = new List<DomNode>();

            var nativeIdMapping = Globals.MEFContainer.GetExportedValue<INativeIdMapping>();
            HashSet<ulong> instanceSet = new HashSet<ulong>();
            foreach (var hit in hits)
                if (instanceSet.Add(hit.instanceId))
                {
                    var nobj = nativeIdMapping.GetAdapter(hit.documentId, hit.instanceId).As<DomNodeAdapter>();
                    if (nobj != null && nobj.DomNode != null)
                        uniqueHits.Add(nobj.DomNode);
                }

            // build 'path' objects for each hit record.
            var paths = new List<AdaptablePath<object>>();
            foreach (var node in uniqueHits)
            {
                var hitPath = Util.AdaptDomPath(node);
                var obj = DesignView.PickFilter.Filter(hitPath, e);
                if (obj != null)
                {
                    var path = obj as AdaptablePath<object> ?? Util.AdaptDomPath((DomNode)obj);
                    // Prevent the same object from being added multiple times...
                    if (paths.Where(x => x.Last == path.Last).FirstOrDefault() == null)
                        paths.Add(path);
                }
            }

            #if false
                if (multiSelect == false && paths.Count > 0 && firstHit != null)
                {
                    var path = paths[0];
                    ISelectionContext selection = DesignView.Context.As<ISelectionContext>();
                    ILinear linear = path.As<ILinear>();
                    if (linear != null
                        && Control.ModifierKeys == System.Windows.Forms.Keys.Shift
                        && selection.SelectionContains(path))
                    {
                        ITransactionContext trans = DesignView.Context.As<ITransactionContext>();
                        trans.DoTransaction(
                            delegate
                            {
                                linear.InsertPoint(firstHit.index, firstHit.hitPt.X, firstHit.hitPt.Y, firstHit.hitPt.Z);
                            }, "insert control point".Localize()
                            );
                    }
                }
            #endif

            return paths;
        }
        
        private IGame TargetGame()
        {
            return DesignView.Context.As<IGame>();
        }

        private readonly List<DomNode> m_ghosts = new List<DomNode>();
        
        protected override void OnDragEnter(DragEventArgs drgevent)
        {
            base.OnDragEnter(drgevent);
            var dragDropTarget = TargetGame();

            foreach (DomNode ghost in m_ghosts) ghost.RemoveFromParent(); 
            m_ghosts.Clear();

            ResourceConverterService resourceConverter = Globals.MEFContainer.GetExportedValue<ResourceConverterService>();
            IEnumerable<object> nodes = Util.ConvertData(drgevent.Data, false);
            foreach (object iterNode in nodes)
            {
                DomNode node = iterNode as DomNode;
                if (node == null)
                {                 
                    if (resourceConverter != null)
                    {
                        var resGob = resourceConverter.Convert(iterNode as IResource);
                        node = resGob.As<DomNode>();                    
                    }                    
                }
                
                if (node == null || node.GetRoot().Is<IGame>())
                    continue;
                
                node.InitializeExtensions();

                var hierarchical = dragDropTarget.AsAll<IHierarchical>();
                bool wasInserted = false;
                foreach (var h in hierarchical)
                    if (h.AddChild(node)) { wasInserted = true; break; }

                if (wasInserted)
                    m_ghosts.Add(node);
            }

            drgevent.Effect = (m_ghosts.Count > 0) ? (DragDropEffects.Move | DragDropEffects.Link) : DragDropEffects.None;
        }

        protected bool GetInsertionPosition(out Vec3F result, Point clientPt)
        {
            var ray = GetWorldRay(clientPt);
            var pick = Picking.RayPick(
                GameEngine.GetEngineDevice(), Adapter.SceneManager,
                ray, Utils.AsCameraDesc(Camera), ClientSize, Picking.Flags.Terrain);

            if (pick != null && pick.Length > 0)
            {
                result = pick[0].hitPt;
                return true;
            }

            // If the ray is off the terrain, find the intersection of the ray with Z=0 plane.
            float distance = ray.Origin.Z / -ray.Direction.Z;
            if (distance > 0.0f)
            {
                result = ray.Origin + distance * ray.Direction;
                return true;
            }

            result = new Vec3F(0.0f, 0.0f, 0.0f);
            return false;
        }
        
        protected override void OnDragOver(DragEventArgs drgevent)
        {        
            base.OnDragOver(drgevent);
            if (DesignView.Context == null || m_ghosts.Count == 0) return;
            
                //  Just do an intersection against the terrain to 
                //  calculate basic insertion position
            Vec3F terrainHit;
            if (GetInsertionPosition(out terrainHit, PointToClient(new Point(drgevent.X, drgevent.Y))))
            {
                ISnapSettings snapSettings = (ISnapSettings)DesignView;
                foreach (var ghost in m_ghosts) {
                    var gameObject = ghost.As<ITransformable>();
                    if (gameObject != null) {
                        gameObject.Translation = terrainHit;

                        // When if terrain alignment mode, we need to query the terrain collision model
                        // for a terrain normal associated with this point. The 
                        if (snapSettings.TerrainAlignment == TerrainAlignmentMode.TerrainUp)
                        {
                            using (var intersectionScene = GameEngine.GetEditorSceneManager().GetIntersectionScene())
                            {
#if GUILAYER_SCENEENGINE
                                float terrainHeight;
                                GUILayer.Vector3 terrainNormal;
                                if (GUILayer.EditorInterfaceUtils.GetTerrainHeightAndNormal(
                                    out terrainHeight, out terrainNormal,
                                    intersectionScene, terrainHit.X, terrainHit.Y))
                                {
                                    gameObject.Rotation = TransformUtils.RotateToVector(
                                        gameObject.Rotation, new Vec3F(terrainNormal.X, terrainNormal.Y, terrainNormal.Z),
                                        Sce.Atf.Rendering.Dom.AxisSystemType.ZIsUp);
                                }
#endif
                            }
                        }
                    }
                }

                DesignView.InvalidateViews();
            }
        }

        protected override void OnDragDrop(DragEventArgs drgevent)
        {
            base.OnDragDrop(drgevent);
            if (DesignView.Context == null) return;

            if (m_ghosts.Count > 0)
            {
                foreach (DomNode ghost in m_ghosts)
                    ghost.RemoveFromParent();

                var dragDropTarget = TargetGame();
                ApplicationUtil.Insert(
                    dragDropTarget,
                    dragDropTarget,
                    m_ghosts,
                    "Drag and Drop",
                    null);

                m_ghosts.Clear();                
                DesignView.InvalidateViews();
            }
        }

        protected override void OnDragLeave(EventArgs e)
        {
            base.OnDragLeave(e);
            if (DesignView.Context == null) return;

            if (m_ghosts.Count > 0)
            {
                foreach (DomNode ghost in m_ghosts) { ghost.RemoveFromParent(); }
                m_ghosts.Clear();                
                DesignView.InvalidateViews();
            }

        }
        protected override void OnPaint(PaintEventArgs e)
        {
            try
            {
                if (DesignView.Context == null || GameEngine.IsInError
                    || GameLoop == null)
                {
                    e.Graphics.Clear(DesignView.BackColor);
                    if (GameEngine.IsInError)
                        e.Graphics.DrawString(GameEngine.CriticalError, Font, Brushes.Red, 1, 1);
                    return;
                }
                GameLoop.Update();
                Render();
            }
            catch (Exception ex)
            {
                e.Graphics.DrawString(ex.Message, Font, Brushes.Red, 1, 1);
            }
        }

        // render the scene.
        public override void Render()
        {
            bool skipRender =
                GameEngine.IsInError
                || Width == 0
                || Height == 0
                || DesignView.Context == null;

            if (skipRender)
                return;

            Adapter.RenderSettings._activeEnvironmentSettings = RenderState.EnvironmentSettings;

            m_clk.Start();
            Adapter.Render();
            m_pendingCaption = string.Format("View Type: {0}   time per frame-render call: {1:0.00} ms", ViewType, m_clk.Milliseconds);

            if (IsPicking)
            {// todo: use Directx to draw marque.                
                using (Graphics g = CreateGraphics())
                {
                    Rectangle rect = MakeRect(FirstMousePoint, CurrentMousePoint);
                    if (rect.Width > 0 && rect.Height > 0)
                    {
                        g.DrawRectangle(s_marqueePen, rect);
                    }
                }
            }

        }

        private void RenderManipulators(GUILayer.SimpleRenderingContext context, DesignView designView)
        {
            var mani = designView.Manipulator;
            if (mani == null) return;

            var extra = mani as IManipulatorExtra;
            var clearBeforeDraw = (extra != null) ? extra.ClearBeforeDraw() : true;
            
            if (clearBeforeDraw) {
                    // disable depth write and depth read
                context.InitState(false, false);
            }

            mani.Render(context, this);
        }

        private void RenderManipulatorsPostProcess(GUILayer.SimpleRenderingContext context, DesignView designView)
        {
            var mani = designView.Manipulator;
            if (mani == null) return;

            var extra = mani as IManipulatorExtra;
            mani.RenderPostProcessing(context, this);
        }

        private void RenderExtras(GUILayer.SimpleRenderingContext context, DesignView designView)
        {
            bool renderSelected = RenderState.DisplayBound == DisplayFlagModes.Selection
                || RenderState.DisplayCaption == DisplayFlagModes.Selection
                || RenderState.DisplayPivot == DisplayFlagModes.Selection;

            if (renderSelected)
            {
                var selection = DesignView.Context.As<ISelectionContext>().Selection;
                IEnumerable<DomNode> rootDomNodes = DomNode.GetRoots(selection.AsIEnumerable<DomNode>());
                RenderProperties(context, rootDomNodes,
                    RenderState.DisplayCaption == DisplayFlagModes.Selection,
                    RenderState.DisplayBound == DisplayFlagModes.Selection,
                    RenderState.DisplayPivot == DisplayFlagModes.Selection);
            }

            if (RenderState.GridMode == RenderState.GridModes.Enabled)
            {
                var game = designView.Context.As<IGame>();
                GridRenderer gridRender = game.Grid.Cast<GridRenderer>();
                gridRender.Render(context, Camera);
            }

            RenderProperties(context, Items,
                RenderState.DisplayCaption == DisplayFlagModes.Always,
                RenderState.DisplayBound == DisplayFlagModes.Always,
                RenderState.DisplayPivot == DisplayFlagModes.Always);

            GameEngine.DrawText2D(m_pendingCaption, Util3D.CaptionFont, 1, 1, Color.White);
        }
        
        private void RenderProperties(GUILayer.SimpleRenderingContext context, IEnumerable<object> objects, bool renderCaption, bool renderBound, bool renderPivot)
        {                      
            if (renderCaption || renderBound)
            {
                Util3D.SetRenderFlag(context, BasicRendererFlags.WireFrame);
                Matrix4F vp = Camera.ViewMatrix * Camera.ProjectionMatrix;
                foreach (object obj in objects)
                {
                    IBoundable bnode = obj.As<IBoundable>();
                    if (bnode == null || bnode.BoundingBox.IsEmpty || obj.Is<IGameObjectFolder>()) continue;

                    INameable nnode = obj.As<INameable>();
                    ITransformable trans = obj.As<ITransformable>();

                    if (renderBound)
                    {
                        Util3D.DrawAABB(context, bnode.BoundingBox);
                    }
                    if (renderCaption && nnode != null)
                    {
                        Vec3F topCenter = bnode.BoundingBox.Center;
                        topCenter.Y = bnode.BoundingBox.Max.Y;
                        Point pt = Project(vp, topCenter);
                        GameEngine.DrawText2D(nnode.Name, Util3D.CaptionFont, pt.X, pt.Y, Color.White);
                    }
                }
            }

            if (renderPivot)
            {
                Util3D.SetRenderFlag(context, BasicRendererFlags.WireFrame | BasicRendererFlags.DisableDepthTest);

                // create few temp matrics to
                Matrix4F toWorld = new Matrix4F();
                Matrix4F PV = new Matrix4F();
                Matrix4F sc = new Matrix4F();
                Matrix4F bl = new Matrix4F();
                Matrix4F recXform = new Matrix4F();
                foreach (object obj in objects)
                {
                    ITransformable trans = obj.As<ITransformable>();
                    IBoundable bnode = obj.As<IBoundable>();
                    if (trans == null || bnode == null || bnode.BoundingBox.IsEmpty || obj.Is<IGameObjectFolder>()) continue;

                    Path<DomNode> path = new Path<DomNode>(trans.Cast<DomNode>().GetPath());
                    toWorld.Set(Vec3F.ZeroVector);
                    TransformUtils.CalcPathTransform(toWorld, path, path.Count - 1);

                    // Offset by pivot
                    PV.Set(trans.Pivot);
                    toWorld.Mul(PV, toWorld);
                    Vec3F pos = toWorld.Translation;

                    const float pivotDiameter = 16; // in pixels
                    float s = Util.CalcAxisScale(Camera, pos, pivotDiameter, Height);                    
                    sc.Scale(s);
                    Util.CreateBillboard(bl, pos, Camera.WorldEye, Camera.Up, Camera.LookAt);
                    recXform = sc * bl;
                    Util3D.DrawPivot(context, recXform, Color.Yellow);
                }
            }
        }
       
        private IEnumerable<object> Items
        {
            get
            {
                IGameDocumentRegistry gameDocumentRegistry = Globals.MEFContainer.GetExportedValue<IGameDocumentRegistry>();
                var doc = gameDocumentRegistry.MasterDocument.As<IEnumerableContext>();
                if (doc != null) return doc.Items;
                return System.Linq.Enumerable.Empty<DomNode>();
            }
        }
        
        private Rectangle MakeRect(Point p1, Point p2)
        {
            int minx = Math.Min(p1.X, p2.X);
            int miny = Math.Min(p1.Y, p2.Y);
            int maxx = Math.Max(p1.X, p2.X);
            int maxy = Math.Max(p1.Y, p2.Y);
            int w = maxx - minx;
            int h = maxy - miny;
            return new Rectangle(minx, miny, w, h);
        }
        
        private static Pen s_marqueePen;
        private Clock m_clk = new Clock();
        private RenderState m_renderState;
        private string m_pendingCaption;


#region IViewContext members
        Size GUILayer.IViewContext.ViewportSize { get { return base.Size; } }
        GUILayer.CameraDescWrapper GUILayer.IViewContext.Camera { get { return Utils.AsCameraDesc(base.Camera); } }
        GUILayer.EditorSceneManager GUILayer.IViewContext.SceneManager { get { return Adapter.SceneManager; } }
        GUILayer.EngineDevice GUILayer.IViewContext.EngineDevice { get { return Adapter.EngineDevice; } }
#endregion
    }
}
