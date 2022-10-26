//Copyright © 2014 Sony Computer Entertainment America LLC. See License.txt.

using System;
using System.Collections.Generic;
using System.ComponentModel.Composition;
using System.IO;
using System.Windows.Forms;
using System.Xml;

using Sce.Atf;
using Sce.Atf.Adaptation;
using Sce.Atf.Applications;
using Sce.Atf.Dom;

using LevelEditorCore;


namespace RenderingInterop
{
    /// <summary>
    /// Native Game Editor.</summary>
    [Export(typeof(IInitializable))]
    [Export(typeof(IControlHostClient))]
    [PartCreationPolicy(CreationPolicy.Shared)]
    public class NativeGameEditor : IInitializable, IControlHostClient
    {
        #region IInitializable Members
        void IInitializable.Initialize()
        {
            m_controlInfo = new ControlInfo("DesignView", "DesignView", StandardControlGroup.CenterPermanent);
            m_controlHostService.RegisterControl(m_designView.HostControl, m_controlInfo, this);
          
            Application.ApplicationExit += delegate
            {
                Util3D.Shutdown();
                GameEngine.Shutdown();
            };

            GameEngine.RefreshView += (sender,e)=> m_designView.InvalidateViews();

            m_gameDocumentRegistry.DocumentAdded += m_gameDocumentRegistry_DocumentAdded;
            m_gameDocumentRegistry.DocumentRemoved += m_gameDocumentRegistry_DocumentRemoved;
                
            string ns = m_schemaLoader.NameSpace;

            // register GridRenderer on grid child.
            DomNodeType gridType = m_schemaLoader.TypeCollection.GetNodeType(ns, "gridType");
            gridType.Define(new ExtensionInfo<GridRenderer>());

            // register NativeGameWorldAdapter on game type.
            m_schemaLoader.GameType.Define(new ExtensionInfo<NativeDocumentAdapter>());

            // parse schema annotation.
            foreach (DomNodeType domType in m_schemaLoader.TypeCollection.GetNodeTypes())
            {
                var topLevelAnnotations = domType.GetTagLocal<IEnumerable<XmlNode>>();
                if (topLevelAnnotations == null)
                    continue;

                    // First, go through and interpret the annotations that are not inherited
                foreach (XmlNode annot in topLevelAnnotations)
                {
                    XmlElement elm = annot as XmlElement;
                    if (elm.LocalName == NativeAnnotations.NativeType)
                    {
                        string typeName = elm.GetAttribute(NativeAnnotations.NativeName);
                        domType.SetTag(NativeAnnotations.NativeType, GameEngine.GetObjectTypeId(typeName));
                        if (domType.IsAbstract == false)
                            domType.Define(new ExtensionInfo<NativeObjectAdapter>());
                    }
                    else if (elm.LocalName == NativeAnnotations.NativeDocumentType)
                    {
                        string typeName = elm.GetAttribute(NativeAnnotations.NativeName);
                        domType.SetTag(NativeAnnotations.NativeDocumentType, GameEngine.GetDocumentTypeId(typeName));
                        if (domType.IsAbstract == false)
                            domType.Define(new ExtensionInfo<NativeDocumentAdapter>());
                    }
                }

                if (domType.GetTag(NativeAnnotations.NativeType) == null) continue;
                uint typeId = (uint)domType.GetTag(NativeAnnotations.NativeType);
                bool isBoundableType = false;

                // Now, go through and interpret annotations that can be inheritted from base clases.
                // Sometimes a native property can be inheritted from a base class. In this model, we
                // will create a separate "property id" for each concrete class. When a property is 
                // inheritted, the "property ids" for each type in the inheritance chain will be different
                // and unrelated.

                List<NativeAttributeInfo> nativeAttribs = new List<NativeAttributeInfo>(); 
                foreach (var inherittedType in domType.Lineage)
                {
                    var annotations = inherittedType.GetTagLocal<IEnumerable<XmlNode>>();
                    if (annotations == null)
                        continue;

                    foreach (XmlNode annot in annotations)
                    {
                        XmlElement elm = annot as XmlElement;
                        if (elm.LocalName == NativeAnnotations.NativeProperty)
                        {
                            // find a prop name and added to the attribute.
                            string nativePropName = elm.GetAttribute(NativeAnnotations.NativeName);
                            string attribName = elm.GetAttribute(NativeAnnotations.Name);
                            string transposeAttrib = elm.GetAttribute("transpose");
                            bool transpose = !string.IsNullOrEmpty(transposeAttrib) && transposeAttrib.Equals("true", StringComparison.CurrentCultureIgnoreCase);
                            uint propId = GameEngine.GetObjectPropertyId(typeId, nativePropName);
                            if (!string.IsNullOrEmpty(attribName))
                            {
                                AttributeInfo attribInfo = domType.GetAttributeInfo(elm.GetAttribute(NativeAnnotations.Name));
                                attribInfo.SetTag(NativeAnnotations.NativeProperty, propId);
                                if (transpose)
                                    attribInfo.SetTag(NativeAnnotations.NativeTranspose, true);
                            }
                            else
                            {
                                NativeAttributeInfo attribInfo = new NativeAttributeInfo(domType, nativePropName, typeId, propId, transpose);
                                nativeAttribs.Add(attribInfo);
                            }

                            if (nativePropName == "Bounds" || nativePropName == "LocalBounds")
                            {
                                isBoundableType = true;
                            }
                        }
                        else if (elm.LocalName == NativeAnnotations.NativeElement)
                        {
                            ChildInfo info = domType.GetChildInfo(elm.GetAttribute(NativeAnnotations.Name));
                            string name = elm.GetAttribute(NativeAnnotations.NativeName);
                            info.SetTag(NativeAnnotations.NativeElement, GameEngine.GetObjectChildListId(typeId, name));
                        }
                        else if (elm.LocalName == NativeAnnotations.NativeVis)
                        {
                            using (var transfer = new NativeObjectAdapter.NativePropertyTransfer())
                            {
                                using (var stream = transfer.CreateStream())
                                    foreach (var a in elm.Attributes)
                                    {
                                        var attrib = a as XmlAttribute;
                                        if (attrib.Name == "geo")
                                        {
                                            NativeObjectAdapter.PushAttribute(
                                                0,
                                                typeof(string), 1,
                                                attrib.Value,
                                                transfer.Properties, stream);
                                        }
                                    }

                                GameEngine.SetTypeAnnotation(typeId, "vis", transfer.Properties);
                            }
                        }
                    }
                }

                if(nativeAttribs.Count > 0)
                {
                    domType.SetTag(nativeAttribs.ToArray());
                }

                if (isBoundableType && domType.IsAbstract == false)
                    domType.Define(new ExtensionInfo<BoundableObject>());
            }

            
            // register BoundableObject
            m_schemaLoader.GameObjectFolderType.Define(new ExtensionInfo<BoundableObject>());   // doesn't have a bound native attributes -- is this really intended?s
            
            #region code to handle gameObjectFolder

            {
                // This code is fragile and need to be updated whenever 
                // any relevant part of the schema changes.
                // purpose:
                // gameObjectFolderType does not exist in C++
                // this code will map gameObjectFolderType to gameObjectGroupType.
                DomNodeType gobFolderType = m_schemaLoader.GameObjectFolderType;
                DomNodeType groupType = m_schemaLoader.GameObjectGroupType;

                // map native bound attrib from gameObject to GobFolder
                NativeAttributeInfo[] nativeAttribs = m_schemaLoader.GameObjectType.GetTag<NativeAttributeInfo[]>();
                foreach (var attrib in nativeAttribs)
                {
                    if (attrib.Name == "Bounds")
                    {
                        gobFolderType.SetTag(new NativeAttributeInfo[] {attrib});
                        break;
                    }
                }

                // map type.
                //      XLE --> Separate GameObjectFolder type from GameObjectGroup type
                // gobFolderType.Define(new ExtensionInfo<NativeObjectAdapter>());
                // gobFolderType.SetTag(NativeAnnotations.NativeType, groupType.GetTag(NativeAnnotations.NativeType));

                // map all native attributes of gameObjectGroup to gameFolder
                foreach (AttributeInfo srcAttrib in groupType.Attributes)
                {
                    object nativeIdObject = srcAttrib.GetTag(NativeAnnotations.NativeProperty);
                    if (nativeIdObject == null) continue;
                    AttributeInfo destAttrib = gobFolderType.GetAttributeInfo(srcAttrib.Name);
                    if (destAttrib == null) continue;
                    destAttrib.SetTag(NativeAnnotations.NativeProperty, nativeIdObject);
                    destAttrib.SetTag(NativeAnnotations.MappedAttribute, srcAttrib);
                }

                // map native element from gameObjectGroupType to gameObjectFolderType.
                object gobsId = groupType.GetChildInfo("object").GetTag(NativeAnnotations.NativeElement);
                foreach (ChildInfo srcChildInfo in gobFolderType.Children)
                {
                    if (srcChildInfo.IsList)
                    {
                        srcChildInfo.SetTag(NativeAnnotations.NativeElement, gobsId);
                    }
                }

                m_schemaLoader.GameType.GetChildInfo("gameObjectFolder").SetTag(NativeAnnotations.NativeElement, gobsId);
            }

            #endregion

            // set up scripting bindings
            if (m_scriptingService != null)
            {
                m_scriptingService.SetVariable("cv", new GUILayer.TweakableBridge());
            }
        }

        #endregion

        #region IControlHostClient Members

        void IControlHostClient.Activate(Control control)
        {
            if (m_designView.Context != null)
                m_contextRegistry.ActiveContext = m_designView.Context;
        }

        void IControlHostClient.Deactivate(Control control)
        {

        }

        bool IControlHostClient.Close(Control control)
        {
            if (m_documentRegistry.ActiveDocument != null)
            {
                return m_documentService.Close(m_documentRegistry.ActiveDocument);
            }

            return true;
        }

        #endregion
    
        private void m_gameDocumentRegistry_DocumentAdded(object sender, ItemInsertedEventArgs<IGameDocument> e)
        {
            IGameDocument document = e.Item;
            if (document == m_gameDocumentRegistry.MasterDocument)
            {
                IGame game = document.As<IGame>();
                if (game != null && game.Grid != null)
                    game.Grid.Cast<GridRenderer>().CreateVertices();

                var context = document.As<IGameContext>();
                if (context!=null)
                    m_designView.Context = context;
            }
        }

        private void m_gameDocumentRegistry_DocumentRemoved(object sender, ItemRemovedEventArgs<IGameDocument> e)
        {
            IGameDocument document = e.Item;
            IGame game = e.Item.As<IGame>();
            if (game != null && game.Grid != null)
                game.Grid.Cast<GridRenderer>().DeleteVertexBuffer();

            var context = document.As<IGameContext>();
            if (context == m_designView.Context)
                m_designView.Context = null;

            var nativeAdapter = document.As<NativeDocumentAdapter>();
            if (nativeAdapter != null)
                nativeAdapter.OnDocumentRemoved();
        }
       

        [Import(AllowDefault = false)]
        private IDocumentRegistry m_documentRegistry;

        [Import(AllowDefault = false)]
        private IDocumentService m_documentService;

        [Import(AllowDefault = false)]
        private IContextRegistry m_contextRegistry;

        [Import(AllowDefault = false)]
        private IControlHostService m_controlHostService;

        [Import(AllowDefault = false)]
        private DesignView m_designView;

        [Import(AllowDefault = false)]
        private ISchemaLoader m_schemaLoader = null;

        [Import(AllowDefault = false)]
        private IGameDocumentRegistry m_gameDocumentRegistry = null;

        [Import(AllowDefault = true)]
        private ScriptingService m_scriptingService = null;

        private ControlInfo m_controlInfo;
    }
}
