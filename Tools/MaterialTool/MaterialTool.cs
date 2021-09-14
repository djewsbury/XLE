// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

using System;
using System.Collections.Generic;
using System.ComponentModel.Composition;
using System.ComponentModel.Composition.Hosting;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Text;
using System.Windows.Forms;

using Sce.Atf;
using Sce.Atf.Adaptation;
using Sce.Atf.Applications;
using Sce.Atf.Controls;
using Sce.Atf.Controls.Adaptable;

#pragma warning disable 0649        // Field '...' is never assigned to, and will always have its default value null

namespace MaterialTool
{
    [Export(typeof(IDocumentClient))]
    [Export(typeof(Editor))]
    [Export(typeof(IInitializable))]
    [PartCreationPolicy(CreationPolicy.Shared)]
    public class Editor : IDocumentClient, IControlHostClient, IInitializable
    {
        [ImportingConstructor]
        public Editor(
            IControlHostService controlHostService,
            ICommandService commandService,
            IContextRegistry contextRegistry,
            IDocumentRegistry documentRegistry,
            IDocumentService documentService,
            LayerLister layerLister)
        {
            m_controlHostService = controlHostService;
            m_commandService = commandService;
            m_contextRegistry = contextRegistry;
            m_documentRegistry = documentRegistry;
            m_documentService = documentService;
            m_layerLister = layerLister;

            // string initialDirectory = Path.Combine(Directory.GetCurrentDirectory(), "..\\..\\..\\..\\components\\wws_atf\\Samples\\CircuitEditor\\data");
            // EditorInfo.InitialDirectory = initialDirectory;
        }

        private IControlHostService m_controlHostService;
        private ICommandService m_commandService;
        private IContextRegistry m_contextRegistry;
        private IDocumentRegistry m_documentRegistry;
        private IDocumentService m_documentService;
        private LayerLister m_layerLister;

        [Import(AllowDefault = true)]
        private IStatusService m_statusService = null;
        [Import(AllowDefault = true)]
        private ISettingsService m_settingsService = null;
        [Import]
        private IFileDialogService m_fileDialogService = null;

        [ImportMany]
        private IEnumerable<Lazy<IContextMenuCommandProvider>> m_contextMenuCommandProviders = null;

        // scripting related members
        [Import(AllowDefault = true)]
        private ScriptingService m_scriptingService = null;

        #region IInitializable

        void IInitializable.Initialize()
        {
            if (m_scriptingService != null)
            {
                // load this assembly into script domain.
                m_scriptingService.LoadAssembly(GetType().Assembly);
                m_scriptingService.ImportAllTypes("CircuitEditorSample");
                m_scriptingService.ImportAllTypes("Sce.Atf.Controls.Adaptable.Graphs");

                m_scriptingService.SetVariable("editor", this);
                m_scriptingService.SetVariable("layerLister", m_layerLister);

                m_contextRegistry.ActiveContextChanged += delegate
                    {
                        var editingContext = m_contextRegistry.GetActiveContext<DiagramEditingContext>();
                        var viewContext = m_contextRegistry.GetActiveContext<HyperGraphAdapter.ViewingContext>();
                        IHistoryContext hist = m_contextRegistry.GetActiveContext<IHistoryContext>();
                        m_scriptingService.SetVariable("editingContext", editingContext);
                        m_scriptingService.SetVariable("view", viewContext);
                        m_scriptingService.SetVariable("hist", hist);
                    };

                // attach tweakable bridge for accessing console variables
                m_scriptingService.SetVariable("cv", new GUILayer.TweakableBridge());
            }

            if (m_settingsService != null)
            {
                // var settings = new[] 
                // {
                //   new BoundPropertyDescriptor(typeof (CircuitDefaultStyle),
                //         () => CircuitDefaultStyle.EdgeStyle,
                //         "Wire Style".Localize(), "Circuit Editor".Localize(),
                //         "Default Edge Style".Localize()),
                // };
                // m_settingsService.RegisterUserSettings("Circuit Editor", settings);
                // m_settingsService.RegisterSettings(this, settings);
            }

                // We need to make sure there is a material set to the active
                // material context... If there is none, we must create a new
                // untitled material, and set that...
            if (_activeMaterialContext.MaterialName == null)
                _activeMaterialContext.MaterialName = GUILayer.RawMaterial.CreateUntitled().Initializer;
        }

        #endregion

        #region IDocumentClient Members

        public DocumentClientInfo Info { get { return EditorInfo; } }
        public static DocumentClientInfo EditorInfo =
            new DocumentClientInfo(
                "Shader Graph".Localize(),
                new string[] { ".graph", ".tech", ".hlsl", ".txt" },
                null, null, false)
            { DefaultExtension = ".graph" };

        public bool CanOpen(Uri uri) { return EditorInfo.IsCompatibleUri(uri); }

        public IDocument Open(Uri uri)
        {
            var doc = _exportProvider.GetExport<DiagramDocument>().Value;

            // When creating a new document, we'll pass through here with a file that
            // doesn't exist... So let's check if we need to load it now...
            if (File.Exists(uri.LocalPath))
            {
                doc.Load(uri);
            }
            else
            {
                doc.InitializeNew();
            }

            doc.Uri = uri;
            doc.GraphMetaData.DefaultsMaterial = _activeMaterialContext.MaterialName;
            doc.GraphMetaData.PreviewModelFile = "rawos/game/model/galleon/galleon.dae";

            var subgraphContext = _exportProvider.GetExport<DiagramEditingContext>().Value;
            subgraphContext.Document = doc;
            var control = _exportProvider.GetExport<Controls.IGraphControl>().Value;
            control.SetContext(subgraphContext);

                // Create a control for the new document, and register it!
            _controlRegistry.RegisterControl(
                doc, control.As<Control>(), 
                new ControlInfo(Path.GetFileName(uri.LocalPath), uri.LocalPath, StandardControlGroup.Center) { IsDocument = true }, 
                this);
            return doc;
        }

        public void Show(IDocument document)
        {
            // Our viewing context is independent of the document... we would need to search through all of the
            // controls to find one that is open to this document...?
            var ctrl = _controlRegistry.DiagramControls.Where(x => x.Key == document).FirstOrDefault();
            if (ctrl.Key == document)
                m_controlHostService.Show(ctrl.Value.First);
        }

        public void Save(IDocument document, Uri uri)
        {
            var doc = (DiagramDocument)document;
            doc.Save(uri);
            doc.Uri = uri;
        }

        public void Close(IDocument document)
        {
            m_documentRegistry.Remove(document);
        }

        #endregion

        #region IControlHostClient Members

        public void Activate(Control control)
        {
            AdaptableControl adaptableControl = (AdaptableControl)control;

            var context = adaptableControl.ContextAs<HyperGraphAdapter.AdaptableSet>();
            if (context != null)
            {
                m_contextRegistry.ActiveContext = context;

                var circuitDocument = context.As<DiagramEditingContext>();
                if (circuitDocument != null)
                    m_documentRegistry.ActiveDocument = circuitDocument.Document as IDocument;
            }
        }

        public void Deactivate(Control control) {}

        public bool Close(Control control)
        {
            var adaptableControl = (AdaptableControl)control;

            bool closed = true;
            var doc = adaptableControl.ContextAs<DiagramEditingContext>();
            if (doc != null)
            {
                closed = m_documentService.Close(doc.Document as IDocument);
                if (closed)
                    Close(doc.Document as IDocument);
            }
            else
            {
                // We don't care if the control was already unregistered. 'closed' should be true.
                _controlRegistry.UnregisterControl(control);
            }
            return closed;
        }

        #endregion

        /// <summary>
        /// Gets and sets a string to be used as the initial directory for the open/save dialog box
        /// regardless of whatever directory the user may have previously navigated to. The default
        /// value is null. Set to null to cancel this behavior.</summary>
        public string InitialDirectory
        {
            get { return m_fileDialogService.ForcedInitialDirectory; }
            set { m_fileDialogService.ForcedInitialDirectory = value; }
        }

        private void control_HoverStarted(object sender, HoverEventArgs<object, object> e)
        {
            _hoverForm = GetHoverForm(e);
        }

        private HoverBase GetHoverForm(HoverEventArgs<object, object> e)
        {
            HoverBase result = CreateHoverForm(e);

            if (result != null)
            {
                Point p = Control.MousePosition;
                result.Location = new Point(p.X - (result.Width + 12), p.Y + 12);
                result.ShowWithoutFocus();
            }
            return result;
        }

        // create hover form for module or connection
        private HoverBase CreateHoverForm(HoverEventArgs<object, object> e)
        {
            // StringBuilder sb = new StringBuilder();
            // 
            // var hoverItem = e.Object;
            // var hoverPart = e.Part;
            // 
            // if (e.SubPart.Is<GroupPin>())
            // {
            //     sb.Append(e.SubPart.Cast<GroupPin>().Name);
            //     CircuitUtil.GetDomNodeName(e.SubPart.Cast<DomNode>());
            // }
            // else if (e.SubObject.Is<DomNode>())
            // {
            //     CircuitUtil.GetDomNodeName(e.SubObject.Cast<DomNode>());
            // }
            // else if (hoverPart.Is<GroupPin>())
            // {
            //     sb.Append(hoverPart.Cast<GroupPin>().Name);
            //     CircuitUtil.GetDomNodeName(hoverPart.Cast<DomNode>());
            // }
            // else if (hoverItem.Is<DomNode>())
            // {
            //     CircuitUtil.GetDomNodeName(hoverItem.Cast<DomNode>());
            // }
            // 
            // HoverBase result = null;
            // if (sb.Length > 0) // remove trailing '\n'
            // {
            //     //sb.Length = sb.Length - 1;
            //     result = new HoverLabel(sb.ToString());
            // }
            // 
            // return result;
            return null;
        }

        private void control_HoverStopped(object sender, EventArgs e)
        {
            if (_hoverForm != null)
            {
                _hoverForm.Controls.Clear();
                _hoverForm.Close();
                _hoverForm.Dispose();
            }
        }

        [Import]
        private DiagramControlRegistry _controlRegistry = null;
        private HoverBase _hoverForm;

        [Import]
        private ExportProvider _exportProvider;

        [Import]
        private ControlsLibraryExt.Material.ActiveMaterialContext _activeMaterialContext;
    }
}