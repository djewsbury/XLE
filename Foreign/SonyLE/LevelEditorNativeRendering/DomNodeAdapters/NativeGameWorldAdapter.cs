//Copyright © 2014 Sony Computer Entertainment America LLC. See License.txt.

using Sce.Atf.Dom;
using Sce.Atf.Adaptation;

namespace RenderingInterop
{
    public class NativeDocumentAdapter : DomNodeAdapter, XLEBridgeUtils.INativeDocumentAdapter
    {
        protected override void OnNodeSet()
        {
            base.OnNodeSet();

            // we must register this document and get an id for it
            DomNode node = this.DomNode; 
            var tag = node.Type.GetTag(NativeAnnotations.NativeDocumentType);
            var typeId = (tag != null) ? (uint)tag : 0;
            m_nativeDocId = GameEngine.CreateDocument(typeId);

            ManageNativeObjectLifeTime = true;
            foreach (var subnode in node.Subtree)
            {
                var childObject = subnode.As<XLEBridgeUtils.INativeObjectAdapter>();
                if (childObject != null)
                {
                    childObject.OnAddToDocument(this);

                    var parentObject = subnode.Parent.As<XLEBridgeUtils.INativeObjectAdapter>();
                    if (parentObject != null)
                    {
                        childObject.OnSetParent(parentObject, GetChildListId(childObject.As<DomNode>().ChildInfo), -1);
                    }
                }
            }
            
            node.ChildInserted += node_ChildInserted;
            node.ChildRemoved += node_ChildRemoved;
        }

        private uint GetChildListId(ChildInfo cinfo)
        {
            if (cinfo == null) return 0u;
            object childListTag = cinfo.GetTag(NativeAnnotations.NativeElement);
            return (childListTag != null) ? (uint)childListTag : 0u;
        }

        public void OnDocumentRemoved()
        {
                // We need to call OnRemoveFromDocument on all children
                // NativeObjectAdapter.OnRemoveFromDocument is now non-hierarchical.
                // It will only have an effect on that particular object, not it's children.
                // As a result, we must iterate through the entire subtree
            DomNode node = this.DomNode; 
            if (node != null)
            {
                foreach (var subnode in node.Subtree)
                {
                    var childObject = subnode.AsAll<XLEBridgeUtils.INativeObjectAdapter>();
                    foreach (var c in childObject)
                        c.OnRemoveFromDocument(this);
                }
            }

                // destroy the document on the native side, as well
            var tag = node.Type.GetTag(NativeAnnotations.NativeDocumentType);
            var typeId = (tag != null) ? (uint)tag : 0;
            GameEngine.DeleteDocument(m_nativeDocId);
        }

        void node_ChildInserted(object sender, ChildEventArgs e)
        {
            node_ChildInserted_Internal(e.Child, e.Parent, e.ChildInfo, e.Index);
        }

        private void node_ChildInserted_Internal(object child, object parent, ChildInfo childInfo, int insertionPoint)
        {
            var childObject = child.As<XLEBridgeUtils.INativeObjectAdapter>();
            if (childObject != null)
            {
                childObject.OnAddToDocument(this);

                var parentObject = parent.As<XLEBridgeUtils.INativeObjectAdapter>();
                if (parentObject != null)
                {
                    childObject.OnSetParent(parentObject, GetChildListId(childInfo), insertionPoint);
                }
                else
                {
                    childObject.OnSetParent(null, GetChildListId(childInfo), -1);
                }
            }

            var children = child.As<DomNode>().Children;
            int index = 0;
            foreach (var c in children) {
                node_ChildInserted_Internal(c, child, c.ChildInfo, index);
                ++index;
            }
        }

        void node_ChildRemoved(object sender, ChildEventArgs e)
        {
            AttemptRemoveNative(e.Child);
        }

        private void AttemptRemoveNative(DomNode node)
        {
            foreach (var subnode in node.Subtree)
            {
                var nativeObject = subnode.AsAll<XLEBridgeUtils.INativeObjectAdapter>();
                foreach (var n in nativeObject)
                {
                    n.OnSetParent(null, 0u, -1);
                    n.OnRemoveFromDocument(this);
                }
            }
        }
        
        /// <summary>
        /// Gets/Sets whether this adapter  creates native object on child inserted 
        /// and deletes it on child removed.
        /// The defautl is true.
        /// </summary>
        public bool ManageNativeObjectLifeTime
        {
            get;
            set;
        }
        public ulong NativeDocumentId { get { return m_nativeDocId; } }
        private ulong m_nativeDocId;
    }
}
