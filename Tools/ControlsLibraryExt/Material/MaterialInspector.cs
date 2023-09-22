// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#if false       // disabled when removing dependency on XLEBridgeUtils

using System.ComponentModel.Composition;
using System.Windows.Forms;
using System.ComponentModel.Composition.Hosting;

using Sce.Atf;
using Sce.Atf.Applications;

using ControlsLibrary.MaterialEditor;
using System.Reflection;

#pragma warning disable 0649        // Field '...' is never assigned to, and will always have its default value null

namespace ControlsLibraryExt.Material
{
    [Export(typeof(IInitializable))]
    [PartCreationPolicy(CreationPolicy.Shared)]
    public class MaterialInspector : IInitializable
    {
        void IInitializable.Initialize()
        {
            _controls = new HierchicalMaterialControl();

            var exports = _exportProvider.GetExports<MaterialControl.ExtraControls>();
            foreach(var e in exports)
            {
                _controls.FocusedMatControls.AddExtraControls(e.Value.TabName, e.Value);
            }

            _controls.Object = _context.MaterialName;
            _context.OnChange += OnActiveMaterialChange;
            _controlHostService.RegisterControl(
                _controls,
                new ControlInfo("Material inspector".Localize(), "Properties for the picked material".Localize(), StandardControlGroup.Right),
                null);
        }

        void OnActiveMaterialChange()
        {
            // todo -- we need a separate window for the preview now (material preview is no longer embedded
            //          in the hierarchical control)
            // Also, there should be a better way to pull in the list of useable environment setting sets
            //
            // var sceneManager = XLEBridgeUtils.NativeManipulatorLayer.SceneManager;
            // if (sceneManager != null)
            // {
            //     var env = new GUILayer.EnvironmentSettingsSet(sceneManager);
            //     env.AddDefault();
            //     m_controls.EnvironmentSet = env;
            // }
            // m_controls.PreviewModel = Tuple.Create(Context.PreviewModelName, Context.PreviewModelBinding); 
            _controls.Object = _context.MaterialName;
        }

        [Import(AllowDefault = false)] private IControlHostService _controlHostService;
        [Import(AllowDefault = false)] private ActiveMaterialContext _context;
        [Import(AllowDefault = false)] private MaterialSchemaLoader _schemaLoader;
        [Import(AllowDefault = false)] private ExportProvider _exportProvider;
        HierchicalMaterialControl _controls;
    }

    [Export(typeof(MaterialControl.ExtraControls))]
    [PartCreationPolicy(CreationPolicy.NonShared)]
    public class MatTab : MaterialControl.ExtraControls
    {
        [ImportingConstructor]
        public MatTab(MaterialSchemaLoader schemaLoader)
        {
            m_schemaLoader = schemaLoader;

            SuspendLayout();
            m_child = new Sce.Atf.Controls.PropertyEditing.PropertyGrid();

            m_child.Location = new System.Drawing.Point(158, 3);
            m_child.Size = new System.Drawing.Size(341, 188);
            m_child.TabIndex = 1;
            m_child.Dock = DockStyle.Fill;
            m_child.Visible = true;

            Controls.Add(m_child);
            Dock = DockStyle.Fill;
            Padding = new Padding(0);
            ResumeLayout(false);
        }

        public override GUILayer.RawMaterial Object
        {
            set
            {
                if (value != null)
                {
                    m_child.Bind(m_schemaLoader.CreatePropertyContext(value));
                }
                else
                {
                    m_child.Bind(null);
                }
            }
        }

        public override string TabName { get { return "Material"; } }

        private Sce.Atf.Controls.PropertyEditing.PropertyGrid m_child;
        private MaterialSchemaLoader m_schemaLoader; 
    }

    [Export(typeof(MaterialSchemaLoader))]
    [PartCreationPolicy(CreationPolicy.Shared)]
    public class MaterialSchemaLoader : XLEBridgeUtils.DataDrivenPropertyContextHelper
    {
        public IPropertyEditingContext CreatePropertyContext(GUILayer.RawMaterial material)
        {
            var ps = new GUILayer.BasicPropertySource(
                new XLEBridgeUtils.RawMaterialShaderConstants_GetAndSet(material),
                GetPropertyDescriptors("gap:RawMaterial"));
            return new XLEBridgeUtils.PropertyBridge(ps);
        }

        public MaterialSchemaLoader()
        {
            SchemaResolver = new ResourceStreamResolver(Assembly.GetExecutingAssembly(), "ControlsLibraryExt.Material");
            Load("material.xsd");
        }
    };
}

#endif
