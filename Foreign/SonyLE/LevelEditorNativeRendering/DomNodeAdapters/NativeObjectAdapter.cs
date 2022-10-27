//Copyright © 2014 Sony Computer Entertainment America LLC. See License.txt.

using System;
using System.Runtime.InteropServices;
using System.Runtime.Serialization.Formatters.Binary;
using System.IO;

using Sce.Atf.Dom;
using Sce.Atf.Adaptation;

using LevelEditorCore;

using System.Collections.Generic;
using Sce.Atf.Controls.Adaptable.Graphs;

namespace RenderingInterop
{
    using PropertyInitializer = GUILayer.EntityLayer.PropertyInitializer;

    /// <summary>
    /// Adapter for DomNode that have runtime counterpart.
    /// It hold native object id and pushes dom changes to native object</summary>
    public class NativeObjectAdapter : DomNodeAdapter, INativeObject, XLEBridgeUtils.INativeObjectAdapter
    {
        protected override void OnNodeSet()
        {
            base.OnNodeSet();            
            DomNode node = DomNode;           
            node.AttributeChanged += node_AttributeChanged;            
            TypeId = (uint)DomNode.Type.GetTag(NativeAnnotations.NativeType);
            _localToWorldAttribute = GetSecondaryAttribute(node.Type, "LocalToWorld");
            _visibleHierarchyAttribute = GetSecondaryAttribute(node.Type, "VisibleHierarchy");

            System.Diagnostics.Debug.Assert(m_instanceId==0);

            // <<XLE    
            //      moveing child initialisation to here (from 
            //      NativeGameEditor::m_gameDocumentRegistry_DocumentAdded)
            //      We need to initialise the children of
            //      non-game documents (but the previous path only worked for game documents)
            //
            //      Maybe neither is ideal. We should do this after a "resolve" event
            //      for the document. 
            CreateNativeObject();
            // XLE>>
        }

        public void OnRemoveFromDocument(XLEBridgeUtils.INativeDocumentAdapter doc)
        {
            if (DomNode == null || m_instanceId == 0) return;

            ulong documentId = (doc != null) ? doc.NativeDocumentId : 0;
            System.Diagnostics.Debug.Assert(documentId == m_documentId);

            GameEngine.DestroyObject(m_documentId, m_instanceId);
            GameEngine.DeregisterGob(m_documentId, m_instanceId, this);
            ReleaseNativeHandle();
        }

        public void OnAddToDocument(XLEBridgeUtils.INativeDocumentAdapter doc)
        {
            var node = DomNode;
            var docTest = (node != null) ? node.GetRoot().As<NativeDocumentAdapter>() : null;
            System.Diagnostics.Debug.Assert(doc == docTest);
            CreateNativeObject();
        }

        public void OnSetParent(XLEBridgeUtils.INativeObjectAdapter newParent, uint childListId, int insertionPosition)
        {
            var p = newParent as NativeObjectAdapter;
            if (p != null)
            {
                GameEngine.SetObjectParent(
                    m_documentId,
                    InstanceId,
                    p.InstanceId, childListId, insertionPosition);
            }
            else
            {
                GameEngine.SetObjectParent(
                    m_documentId,
                    InstanceId,
                    0, childListId, insertionPosition);
            }

            UpdateLocalToWorld();
            UpdateLocalToWorld_Children();
        }

        internal void UpdateLocalToWorld()
        {
            using (var transfer = new NativePropertyTransfer())
            {
                using (var stream = transfer.CreateStream())
                {
                    UpdateSecondaryNativeProperties(transfer.Properties, stream);
                    if (transfer.Properties.Count > 0)
                    {
                        GameEngine.SetObjectProperty(
                            DocumentId, InstanceId,
                            transfer.Properties);
                    }
                }
            }
        }

        internal void UpdateLocalToWorld_Children()
        {
            foreach (DomNode child in DomNode.Subtree)
            {
                NativeObjectAdapter childObject = child.As<NativeObjectAdapter>();
                if (childObject != null)
                    childObject.UpdateLocalToWorld();
            }
        }

        internal class NativePropertyTransfer : IDisposable
        {
            public IList<PropertyInitializer> Properties { get { return m_properties;  } }

            void IDisposable.Dispose()
            {
                Marshal.FreeHGlobal(m_bufferHandle);
            }

            public unsafe System.IO.UnmanagedMemoryStream CreateStream()
            {
                return new System.IO.UnmanagedMemoryStream(
                    (byte*)m_bufferHandle.ToPointer(), 0, s_bufferSize, FileAccess.Write);
            }

            public NativePropertyTransfer()
            {
                m_properties = new List<PropertyInitializer>();
                m_bufferHandle = Marshal.AllocHGlobal(s_bufferSize);
            }

            private IntPtr m_bufferHandle;
            private List<PropertyInitializer> m_properties;
            private const int s_bufferSize = 2048;
        }
        
        void node_AttributeChanged(object sender, AttributeEventArgs e)
        {
            // process events only for the DomNode attached to this adapter.
            if (this.DomNode != e.DomNode)
                return;

            // the "attribInfo" given to us may not belong this this exact
            // object type. It might belong to a base class (or even, possibly, to a super class). 
            // We need to check for these cases, and remap to the exact attribute that
            // belongs our concrete class.
            var type = DomNode.Type;
            var attribInfo = e.AttributeInfo;
            if (attribInfo.DefiningType != type)
            {
                attribInfo = DomNode.Type.GetAttributeInfo(attribInfo.Name);
                if (attribInfo == null) return;
            }

            bool hierarchicalProperty = (e.AttributeInfo.Name == "transform") || (e.AttributeInfo.Name == "visible");
            using (var transfer = new NativePropertyTransfer())
            {
                using (var stream = transfer.CreateStream())
                {
                    UpdateNativeProperty(DomNode, attribInfo, transfer.Properties, stream);
                    if (hierarchicalProperty)
                        UpdateSecondaryNativeProperties(transfer.Properties, stream);
                    if (transfer.Properties.Count > 0)
                    {
                        GameEngine.SetObjectProperty(
                            DocumentId, InstanceId,
                            transfer.Properties);
                    }
                }
            }
            if (hierarchicalProperty) UpdateLocalToWorld_Children();
        }
        
        /// <summary>
        /// Updates all the shared properties  
        /// between this and native object 
        /// only call onece.
        /// </summary>
        public unsafe void UpdateNativeObject()
        {
            using (var transfer = new NativePropertyTransfer())
            {
                using (var stream = transfer.CreateStream())
                {
                    foreach (AttributeInfo attribInfo in this.DomNode.Type.Attributes)
                        UpdateNativeProperty(DomNode, attribInfo, transfer.Properties, stream);
                    UpdateSecondaryNativeProperties(transfer.Properties, stream);
                    if (transfer.Properties.Count > 0)
                    {
                        GameEngine.SetObjectProperty(
                            DocumentId, InstanceId,
                            transfer.Properties);
                    }
                }
            }
        }

        private void CreateNativeObject()
        {
            var node = DomNode;
            if (DomNode == null || m_instanceId != 0) return;

            var doc = node.GetRoot().As<NativeDocumentAdapter>();
            if (doc != null && doc.ManageNativeObjectLifeTime)
            {
                // The object might have a pre-assigned instance id.
                // If so, we should attempt to use that same id.
                // This is important if we want the id to persist between sessions
                //      --  For example, we must reference placement objects through
                //          a GUID value that remains constant. We don't want to create
                //          new temporary ids for placements every time we create them,
                //          when the GUID value is more reliable

                ulong existingId = 0;
                var idField = DomNode.Type.GetAttributeInfo("ID");
                if (idField != null)
                {
                    var id = DomNode.GetAttribute(idField);
                    if (id is UInt64)
                    {
                        existingId = (UInt64)id;
                    }
                    else
                    {
                        string stringId = id as string;
                        if (stringId != null && stringId.Length > 0)
                        {
                            if (!UInt64.TryParse(stringId, out existingId))
                            {
                                existingId = GUILayer.Utils.HashID(stringId);
                            }
                        }
                    }
                }

                using (var transfer = new NativePropertyTransfer())
                {
                    using (var stream = transfer.CreateStream())
                    {
                        foreach (AttributeInfo attribInfo in this.DomNode.Type.Attributes)
                            UpdateNativeProperty(DomNode, attribInfo, transfer.Properties, stream);
                        UpdateSecondaryNativeProperties(transfer.Properties, stream);
                        if (existingId != 0)
                        {
                            bool success = GameEngine.CreateObjectWithId(doc.NativeDocumentId, TypeId, existingId, transfer.Properties);
                            m_instanceId = success ? existingId : 0; 
                        }
                        else
                        {
                            m_instanceId = GameEngine.CreateObject(doc.NativeDocumentId, TypeId, transfer.Properties);
                        }
                    }
                }

                if (m_instanceId != 0)
                {
                    m_documentId = doc.NativeDocumentId;
                    GameEngine.RegisterGob(m_documentId, m_instanceId, this);
                }
            }
        }

        unsafe private static void SetStringProperty(
            uint propId, string str,
            IList<PropertyInitializer> properties,
            System.IO.UnmanagedMemoryStream stream)
        {
            // Go to utf8 here, so the native code doesn't have to care about whatever character
            // encoding C# is using
            var utf8 = System.Text.Encoding.UTF8;
            byte[] utfBytes = utf8.GetBytes(str);

            var length = utfBytes.Length;
            properties.Add(GameEngine.CreateInitializer(
                propId, stream.PositionPointer, stream.PositionPointer + length,
                typeof(byte), (uint)length, true));

            for (uint c = 0; c < length; ++c)
                stream.PositionPointer[c] = utfBytes[c];

                // just advance the stream position over what we've just written
            stream.Position += length;
        }

        unsafe private static void SetBasicProperty<T>(
            uint propId, T obj, uint size,
            IList<PropertyInitializer> properties,
            System.IO.UnmanagedMemoryStream stream)
        {
            properties.Add(GameEngine.CreateInitializer(
                propId, stream.PositionPointer, stream.PositionPointer + size,
                typeof(T), 1, false));
        }

        unsafe internal static void UpdateNativeProperty(
            DomNode node,
            AttributeInfo attribInfo,
            IList<PropertyInitializer> properties, 
            System.IO.UnmanagedMemoryStream stream)
        {
            object idObj = attribInfo.GetTag(NativeAnnotations.NativeProperty);
            if (idObj == null) return;
            uint id = (uint)idObj;

            // Check if this is a matrix we need to transpose before sending to the native side
            if (attribInfo.GetTag(NativeAnnotations.NativeTranspose) != null)
            {
                Type clrType = attribInfo.Type.ClrType;
                Type elmentType = clrType.GetElementType();
                if (clrType.IsArray && elmentType.IsPrimitive && elmentType == typeof(float))
                {
                    PushTransposedMatrixAttribute(
                        id,
                        node.GetAttribute(attribInfo),
                        properties, stream);
                    return;
                }
            }

            PushAttribute(
                id,
                attribInfo.Type.ClrType, attribInfo.Type.Length,
                node.GetAttribute(attribInfo),
                properties, stream);
        }

        internal static NativeAttributeInfo GetSecondaryAttribute(DomNodeType type, string name)
        {
            var secondaryAttribs = type.GetTag<NativeAttributeInfo[]>();
            if (secondaryAttribs != null)
                foreach (var a in secondaryAttribs)
                    if (a.Name.Equals(name))
                        return a;
            return null;
        }

        unsafe internal void UpdateSecondaryNativeProperties(
            IList<PropertyInitializer> properties, 
            System.IO.UnmanagedMemoryStream stream)
        {
            if (_localToWorldAttribute != null)
            {
                // we have to build a "local to world" transform, and push that through.
                var trans = DomNode.As<ITransformable>();
                if (trans != null)
                {
                    var localToWorld = trans.LocalToWorld;
                    properties.Add(
                        GameEngine.CreateInitializer(
                            _localToWorldAttribute.PropertyId, stream.PositionPointer, stream.PositionPointer + sizeof(float) * 16,
                            typeof(float), 16, false));
                    if (_localToWorldAttribute.Transpose)
                    {
                        for (int j = 0; j < 4; ++j)
                            for (int i = 0; i < 4; ++i)
                                ((float*)stream.PositionPointer)[j*4+i] = localToWorld[i*4+j];
                    }
                    else
                    {
                        for (int c = 0; c < 16; ++c)
                            ((float*)stream.PositionPointer)[c] = localToWorld[c];
                    }
                    stream.Position += sizeof(float) * 16;
                }
            }

            if (_visibleHierarchyAttribute != null)
            {
                var visible = DomNode.As<IVisible>();
                if (visible != null)
                {
                    uint d = Convert.ToUInt32(visible.Visible);
                    SetBasicProperty(_visibleHierarchyAttribute.PropertyId, d, sizeof(uint), properties, stream);
                    *(uint*)stream.PositionPointer = d;
                    stream.Position += sizeof(uint);
                }
            }
        }

        unsafe internal static void PushAttribute(
            uint propertyId,
            Type clrType, int arrayLength,
            object data,
            IList<PropertyInitializer> properties, 
            System.IO.UnmanagedMemoryStream stream)
        {
            Type elmentType = clrType.GetElementType();
            if (clrType.IsArray && elmentType.IsPrimitive)
            {
                if (elmentType == typeof(float))
                {
                    var count = Math.Min(arrayLength, ((float[])data).Length);
                    properties.Add(GameEngine.CreateInitializer(
                        propertyId, stream.PositionPointer, stream.PositionPointer + sizeof(float)*count,
                        elmentType, (uint)count, false));

                    fixed (float* d = (float[])data)
                        for (uint c = 0; c < count; ++c)
                            ((float*)stream.PositionPointer)[c] = d[c];
                    stream.Position += sizeof(float) * count;
                }
                else if (elmentType == typeof(int))
                {
                    var count = Math.Min(arrayLength, ((int[])data).Length);
                    properties.Add(GameEngine.CreateInitializer(
                        propertyId, stream.PositionPointer, stream.PositionPointer + sizeof(int) * count,
                        elmentType, (uint)count, false));

                    fixed (int* d = (int[])data)
                        for (uint c = 0; c < count; ++c)
                            ((int*)stream.PositionPointer)[c] = d[c];
                    stream.Position += sizeof(int) * count;
                }

                else if (elmentType == typeof(uint))
                {
                    var count = Math.Min(arrayLength, ((uint[])data).Length);
                    properties.Add(GameEngine.CreateInitializer(
                        propertyId, stream.PositionPointer, stream.PositionPointer + sizeof(uint) * count,
                        elmentType, (uint)count, false));

                    fixed (uint* d = (uint[])data)
                        for (uint c = 0; c < count; ++c)
                            ((uint*)stream.PositionPointer)[c] = d[c];
                    stream.Position += sizeof(uint) * count;
                }
                else
                {
                    System.Diagnostics.Debug.Assert(false);
                }
            }
            else
            {
                if (clrType == typeof(string)) {
                    SetStringProperty(propertyId, (string)data, properties, stream);
                }

                else if (clrType == typeof(bool))   {
                    uint d = Convert.ToUInt32((bool)data);
                    SetBasicProperty(propertyId, d, sizeof(uint), properties, stream);
                    *(uint*)stream.PositionPointer = d;
                    stream.Position += sizeof(uint);
                }

                else if (clrType == typeof(byte))   {
                    SetBasicProperty(propertyId, (byte)data, sizeof(byte), properties, stream);
                    *(byte*)stream.PositionPointer = (byte)data;
                    stream.Position += sizeof(byte);
                }

                else if (clrType == typeof(sbyte))  {
                    SetBasicProperty(propertyId, (sbyte)data, sizeof(sbyte), properties, stream);
                    *(sbyte*)stream.PositionPointer = (sbyte)data;
                    stream.Position += sizeof(sbyte);
                }

                else if (clrType == typeof(short))  {
                    SetBasicProperty(propertyId, (short)data, sizeof(short), properties, stream);
                    *(short*)stream.PositionPointer = (short)data;
                    stream.Position += sizeof(short);
                }

                else if (clrType == typeof(ushort)) {
                    SetBasicProperty(propertyId, (ushort)data, sizeof(ushort), properties, stream);
                    *(ushort*)stream.PositionPointer = (ushort)data;
                    stream.Position += sizeof(ushort);
                }

                else if (clrType == typeof(int)) {
                    SetBasicProperty(propertyId, (int)data, sizeof(int), properties, stream);
                    *(int*)stream.PositionPointer = (int)data;
                    stream.Position += sizeof(int);
                }

                else if (clrType == typeof(uint)) {
                    SetBasicProperty(propertyId, (uint)data, sizeof(uint), properties, stream);
                    *(uint*)stream.PositionPointer = (uint)data;
                    stream.Position += sizeof(uint);
                }

                else if (clrType == typeof(long)) {
                    SetBasicProperty(propertyId, (long)data, sizeof(long), properties, stream);
                    *(long*)stream.PositionPointer = (long)data;
                    stream.Position += sizeof(long);
                }

                else if (clrType == typeof(ulong)) {
                    SetBasicProperty(propertyId, (ulong)data, sizeof(ulong), properties, stream);
                    *(ulong*)stream.PositionPointer = (ulong)data;
                    stream.Position += sizeof(ulong);
                }

                else if (clrType == typeof(float)) {
                    SetBasicProperty(propertyId, (float)data, sizeof(float), properties, stream);
                    *(float*)stream.PositionPointer = (float)data;
                    stream.Position += sizeof(float);
                }

                else if (clrType == typeof(double)) {
                    SetBasicProperty(propertyId, (double)data, sizeof(double), properties, stream);
                    *(double*)stream.PositionPointer = (double)data;
                    stream.Position += sizeof(double);
                }

                else if (clrType == typeof(System.Uri)) {
                    if(data != null)
                    {
                        string assetName = AsAssetName((Uri)data);
                        if (!string.IsNullOrWhiteSpace(assetName))
                            SetStringProperty(propertyId, assetName, properties, stream);
                    }
                }

                else if (clrType == typeof(DomNode)) {
                    // this is a 'reference' to an object
                    DomNode refNode = (DomNode)data;
                    NativeObjectAdapter nativeGob = refNode.As<NativeObjectAdapter>();
                    if(nativeGob != null)
                    {
                        SetBasicProperty(propertyId, (ulong)nativeGob.InstanceId, sizeof(ulong), properties, stream);
                        *(ulong*)stream.PositionPointer = (ulong)nativeGob.InstanceId;
                        stream.Position += sizeof(ulong);
                    }
                }
            }
        }

        unsafe internal static void PushTransposedMatrixAttribute(
            uint propertyId,
            object data,
            IList<PropertyInitializer> properties,
            System.IO.UnmanagedMemoryStream stream)
        {
            var count = 16;
            properties.Add(GameEngine.CreateInitializer(
                propertyId, stream.PositionPointer, stream.PositionPointer + sizeof(float) * count,
                typeof(float), (uint)count, false));

            fixed (float* d = (float[])data)
                for (uint j = 0; j < 4; ++j)
                    for (uint i = 0; i < 4; ++i)
                        ((float*)stream.PositionPointer)[i*4+j] = d[j*4+i];     // transpose here
            stream.Position += sizeof(float) * count;
        }

        private static string AsAssetName(Uri uri)
        {
            string result;
            if (uri.IsAbsoluteUri)
            {
                var cwd = new Uri(System.IO.Directory.GetCurrentDirectory().TrimEnd('\\') + "\\");
                var relUri = cwd.MakeRelativeUri(uri);

                    // If the relative directory beings with a ".." (ie, the file is somewhere else
                    // on the drive, outside of the working folder) then let's pass the full absolute
                    // path. 
                    // This is not ideally. Really we should be using only absolute paths in the C#
                    // side, and leave the native side to deal with converting them to relative when
                    // necessary.
                if (relUri.OriginalString.Substring(0, 2) == "..")
                {
                    result = Uri.UnescapeDataString(uri.LocalPath); // (use LocalPath to avoid the file:// prefix)
                }
                else
                {
                    result = Uri.UnescapeDataString(relUri.OriginalString);
                }
            }
            else
            {
                result = Uri.UnescapeDataString(uri.OriginalString);
            }
            result = "rawos/" + result;

            // awkwardly, we want to make the filename lowercase here; but leave the case there for the parameters
            var paramSeparator = result.LastIndexOf('?');
            if (paramSeparator > 0)
            {
                result = result.Substring(0, paramSeparator).ToLower() + ":" + result.Substring(paramSeparator + 1);
            } 
            else
                result = result.ToLower();
            return result;
        }

        public uint TypeId
        {
            get;
            private set;
        }

        /// <summary>
        /// this method is exclusively used by GameEngine class.                
        public void ReleaseNativeHandle() { m_instanceId = 0; m_documentId = 0; }

        private ulong m_instanceId = 0;
        private ulong m_documentId = 0;

        private NativeAttributeInfo _visibleHierarchyAttribute;
        private NativeAttributeInfo _localToWorldAttribute;
        
        #region INativeObject Members
        public void InvokeFunction(string fn, IntPtr arg, out IntPtr retval)
        {
            GameEngine.InvokeMemberFn(InstanceId,fn, arg, out retval);
        }

        /// <summary>
        /// Gets native id for this instance of the C++ counter part of this object.</summary>
        public ulong InstanceId
        {
            get { return m_instanceId; }

        }

        public ulong DocumentId
        {
            get { return m_documentId; }
        }

        public Tuple<ulong, ulong> NativeHighlightableId
        {
            get { return GameEngine.QueryNativeHighlightableId(m_documentId, m_instanceId); }
        }
        #endregion
    }
}
