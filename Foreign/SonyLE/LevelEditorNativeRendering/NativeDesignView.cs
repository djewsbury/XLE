//Copyright © 2014 Sony Computer Entertainment America LLC. See License.txt.

using System;
using System.ComponentModel.Composition;
using System.Collections.Generic;

using Sce.Atf;
using Sce.Atf.Dom;
using Sce.Atf.Adaptation;
using Sce.Atf.Applications;

using LevelEditorCore;


using ViewTypes = Sce.Atf.Rendering.ViewTypes;

namespace RenderingInterop
{
    [Export(typeof(ISnapSettings))]
    [Export(typeof(IDesignView))]    
    [Export(typeof(DesignView))]
    [PartCreationPolicy(CreationPolicy.Shared)]
    public class NativeDesignView : DesignView
    {        
        public NativeDesignView()
        {
            NativeDesignControl[] views = new NativeDesignControl[]
            {
                new NativeDesignControl(this) { ViewType = ViewTypes.Perspective },
                new NativeDesignControl(this) { ViewType = ViewTypes.Right },
                new NativeDesignControl(this) { ViewType = ViewTypes.Top },
                new NativeDesignControl(this) { ViewType = ViewTypes.Front }
            };

            foreach (var v in views)
            {
                v.Adapter.AddRenderCallback(
                    (GUILayer.SimpleRenderingContext context) => RenderCallback(this, v.Camera));
            }

            QuadView.TopLeft = views[0];
            QuadView.TopRight = views[1];
            QuadView.BottomLeft = views[2];
            QuadView.BottomRight = views[3];

            // set control names.
            QuadView.TopLeft.Name = "TopLeft";
            QuadView.TopRight.Name = "TopRight";
            QuadView.BottomLeft.Name = "BottomLeft";
            QuadView.BottomRight.Name = "BottomRight";

            ViewMode = ViewModes.Single;
            ContextChanged += NativeDesignView_ContextChanged;
        }

        void NativeDesignView_ContextChanged(object sender, EventArgs e)
        {
            if (m_selectionContext != null)
            {
                m_selectionContext.SelectionChanged -= new EventHandler(m_selectionContext_SelectionChanged);
            }

            m_selectionContext = Context.As<ISelectionContext>();

            if (m_selectionContext != null)
            {
                m_selectionContext.SelectionChanged += new EventHandler(m_selectionContext_SelectionChanged);
            }
        }

        void m_selectionContext_SelectionChanged(object sender, EventArgs e)
        {
            var domNodes = m_selectionContext.Selection.AsIEnumerable<DomNode>();
            var roots = DomNode.GetRoots(domNodes);

            var sel = GameEngine.GlobalSelection;
            sel.Clear();
            foreach (var node in roots)
            {
                if (node.Is<ITransformableGroup>())
                {
                    foreach (var adapter in node.Subtree.AsIEnumerable<NativeObjectAdapter>())
                        sel.Add(adapter.NativeHighlightableId);
                }
                else
                {
                    var adapter = node.As<NativeObjectAdapter>();
                    if (adapter != null)
                        sel.Add(adapter.NativeHighlightableId);
                }
            }

            InvalidateViews();
        }

        private ISelectionContext m_selectionContext;

        private void RenderCallback(DesignView designView, Sce.Atf.Rendering.Camera camera) {}
    }

}
