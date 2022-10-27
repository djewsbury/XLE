using System;
using System.Collections.Generic;
using System.ComponentModel.Composition;
using System.Windows.Forms;
using System.Drawing;

using Sce.Atf;
using Sce.Atf.Adaptation;
using Sce.Atf.Applications;
using Sce.Atf.Dom;
using Sce.Atf.VectorMath;

using Sce.Atf.Controls.PropertyEditing;

using LevelEditorCore;
using Camera = Sce.Atf.Rendering.Camera;

#pragma warning disable 0649 // Field '...' is never assigned to, and will always have its default value null

namespace LevelEditorXLE.Materials
{
    [Export(typeof(IManipulator))]
    [PartCreationPolicy(CreationPolicy.Shared)]
    public class PickMaterialManipulator : IManipulator
    {
        public PickMaterialManipulator()
        {
            ManipulatorInfo = new ManipulatorInfo(
                "Pick Material".Localize(),
                "Eyedropper tool for selecting a material".Localize(),
                "",
                Keys.None);
        }

        public ManipulatorPickResult Pick(ViewControl vc, Point scrPt) 
        {
            m_highlightMaterialGUID = ~0ul;
            m_highlight.Clear();

            var ray = vc.GetWorldRay(scrPt);
            var endPt = ray.Origin + vc.Camera.FarZ * ray.Direction;

            var nativeVC = vc as GUILayer.IViewContext;
            if (nativeVC == null) return ManipulatorPickResult.Miss;

            // do an intersection test here, and find the material under the cursor
            var pick = XLEBridgeUtils.Picking.RayPick(
                nativeVC, ray, XLEBridgeUtils.Picking.Flags.Objects);

            if (pick != null && pick.Length > 0)
            {
                m_highlightMaterialGUID = pick[0].materialGuid;
                m_highlight.Add(new Tuple<ulong, ulong>(pick[0].documentId, pick[0].instanceId));
            }

            return ManipulatorPickResult.ImmediateBeginDrag;
        }

        public void OnBeginDrag(ViewControl vc, Point scrPt) {}
        public void OnDragging(ViewControl vc, Point scrPt) {}

        public void OnEndDrag(ViewControl vc, Point scrPt) 
        {
            var ray = vc.GetWorldRay(scrPt);
            var endPt = ray.Origin + vc.Camera.FarZ * ray.Direction;

            // do an intersection test here, and find the material under the cursor
            var pick = XLEBridgeUtils.Picking.RayPick(
                vc as GUILayer.IViewContext, ray, XLEBridgeUtils.Picking.Flags.Objects);

            if (pick != null && pick.Length > 0)
            {
                Context.PreviewModelName = pick[0].modelName;
                Context.PreviewModelBinding = pick[0].materialGuid; 
                Context.MaterialName = pick[0].materialName;
            }

            m_highlightMaterialGUID = ~0ul;
            m_highlight.Clear();
        }

        public void OnMouseWheel(ViewControl vc, Point scrPt, int delta) { }

        public void Render(object opaqueContext, ViewControl vc) { }

        public void RenderPostProcessing(object opaqueContext, ViewControl vc)
        {
            if (m_highlightMaterialGUID == ~0ul) return;

            // ---- ---- ---- ---- render highlight ---- ---- ---- ----
            var nativeVC = vc as GUILayer.IViewContext;
            if (nativeVC == null) return;

            var context = opaqueContext as GUILayer.SimpleRenderingContext;
            if (context == null) return;

            var sceneManager = nativeVC.SceneManager;
            using (var placements = sceneManager.GetPlacementsEditor())
            {
                using (var renderer = sceneManager.GetPlacementsRenderer())
                {
                    GUILayer.RenderingUtil.RenderHighlight(
                        context, placements, renderer,
                        null, m_highlightMaterialGUID);
                }
            }
        }

        public ManipulatorInfo ManipulatorInfo
        {
            get;
            protected set;
        }

        public System.Windows.Forms.Control GetHoveringControl() { return null; }
        public event System.EventHandler OnHoveringControlChanged;

        [Import(AllowDefault = false)] private ControlsLibraryExt.Material.ActiveMaterialContext Context;
        private GUILayer.ObjectSet m_highlight = new GUILayer.ObjectSet();
        private ulong m_highlightMaterialGUID = ~0ul;
    }
}

