// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

using GUILayer;
using System;
using System.Drawing;
using System.Windows.Forms;

namespace ControlsLibraryExt.ModelView
{
    public partial class CtrlStrip : UserControl
    {
        public CtrlStrip()
        {
            InitializeComponent();
            _colByMaterial.DataSource = Enum.GetValues(typeof(GUILayer.VisOverlaySettings.ColourByMaterialType));
            _skeletonMode.DataSource = Enum.GetValues(typeof(SkeletonMode));
            _visualizationType.DataSource = new string[]
            {
                "Diffuse Albedo",
                "Solid Wireframe",
                "World Space Normals",
                "Roughness", "Metal", "Specular", "Cooked AO",
                "World Space Position",
                "Flat Color"
            };
            _drawGrid.Checked = true;
        }

        private enum SkeletonMode
        {
            NoSkeleton,
            Skeleton,
            BoneNames
        };

        public GUILayer.VisOverlaySettings OverlaySettings;
        public GUILayer.ModelVisSettings ModelSettings;
        public PreviewerLightingSettings PreviewerLightingSettings;

        public event EventHandler OverlaySettings_OnChange;
        public event EventHandler ModelSettings_OnChange;
        public event EventHandler PreviewerLightingSettings_OnChange;
        public event EventHandler OnResetCamera;

        private void OverlaySettings_InvokeOnChange()
        {
            if (OverlaySettings_OnChange != null && OverlaySettings != null)
                OverlaySettings_OnChange.Invoke(this, EventArgs.Empty);
        }

        private void ModelSettings_InvokeOnChange()
        {
            if (ModelSettings_OnChange != null && ModelSettings != null)
                ModelSettings_OnChange.Invoke(this, EventArgs.Empty);
        }

        private void PreviewerLightingSettings_InvokeOnChange()
        {
            if (PreviewerLightingSettings_OnChange != null && PreviewerLightingSettings != null)
                PreviewerLightingSettings_OnChange.Invoke(this, EventArgs.Empty);
        }

        private void SelectModel(object sender, EventArgs e)
        {
            // We want to pop up a small drop-down window with a
            // property grid that can be used to set all of the properties
            // in the ModelViewSettings object. This is a handy non-model
            // way to change the selected model

            var dropDownCtrl = new PropertyGrid();
            dropDownCtrl.SelectedObject = ModelSettings;
            dropDownCtrl.ToolbarVisible = false;
            dropDownCtrl.HelpVisible = false;

            var toolDrop = new ToolStripDropDown();
            var toolHost = new ToolStripControlHost(dropDownCtrl);
            toolHost.Margin = new Padding(0);
            toolDrop.Padding = new Padding(0);
            toolDrop.Items.Add(toolHost);

            // we don't have a way to know the ideal height of the drop down ctrl... just make a guess based on the text height
            toolHost.AutoSize = false;
            toolHost.Size = new Size(512, 8 * dropDownCtrl.Font.Height);

            toolDrop.Show(this, PointToClient(MousePosition));
            toolDrop.Closing += ToolDrop_Closing;
        }

        private void ToolDrop_Closing(object sender, ToolStripDropDownClosingEventArgs e)
        {
            ModelSettings_InvokeOnChange();
        }

        private void SelectColorByMaterial(object sender, EventArgs e)
        {
            if (OverlaySettings == null) return;

            GUILayer.VisOverlaySettings.ColourByMaterialType v;
            if (Enum.TryParse(((ComboBox)sender).SelectedValue.ToString(), out v))
            {
                OverlaySettings.ColourByMaterial = v;
                OverlaySettings_InvokeOnChange();
            }
        }

        private void SelectSkeletonMode(object sender, EventArgs e)
        {
            if (OverlaySettings == null) return;

            SkeletonMode v;
            if (Enum.TryParse(((ComboBox)sender).SelectedValue.ToString(), out v))
            {
                switch (v)
                {
                    default:
                    case SkeletonMode.NoSkeleton: OverlaySettings.SkeletonMode = GUILayer.VisOverlaySettings.SkeletonModes.None; break;
                    case SkeletonMode.Skeleton: OverlaySettings.SkeletonMode = GUILayer.VisOverlaySettings.SkeletonModes.Render; break;
                    case SkeletonMode.BoneNames: OverlaySettings.SkeletonMode = GUILayer.VisOverlaySettings.SkeletonModes.BoneNames; break;
                }
                OverlaySettings_InvokeOnChange();
            }
        }

        private void _visualizationType_SelectedIndexChanged(object sender, System.EventArgs e)
        {
            if (PreviewerLightingSettings == null) return;

            var str = ((ComboBox)sender).SelectedValue.ToString();
            if (str == "Diffuse Albedo") PreviewerLightingSettings.UtilityType = GUILayer.UtilityRenderingType.CopyDiffuseAlbedo;
            else if (str == "Solid Wireframe") PreviewerLightingSettings.UtilityType = GUILayer.UtilityRenderingType.SolidWireframe;
            else if (str == "World Space Normals") PreviewerLightingSettings.UtilityType = GUILayer.UtilityRenderingType.CopyWorldSpaceNormal;
            else if (str == "Roughness") PreviewerLightingSettings.UtilityType = GUILayer.UtilityRenderingType.CopyRoughness;
            else if (str == "Metal") PreviewerLightingSettings.UtilityType = GUILayer.UtilityRenderingType.CopyMetal;
            else if (str == "Specular") PreviewerLightingSettings.UtilityType = GUILayer.UtilityRenderingType.CopySpecular;
            else if (str == "Cooked AO") PreviewerLightingSettings.UtilityType = GUILayer.UtilityRenderingType.CopyCookedAO;
            else if (str == "World Space Position") PreviewerLightingSettings.UtilityType = GUILayer.UtilityRenderingType.CopyWorldSpacePosition;
            else PreviewerLightingSettings.UtilityType = GUILayer.UtilityRenderingType.FlatColor;
            PreviewerLightingSettings_InvokeOnChange();
        }

        private void ResetCamClick(object sender, EventArgs e)
        {
            if (OnResetCamera != null)
                OnResetCamera.Invoke(this, null);
        }

        private void visualizationRenderingButton_Click(object sender, EventArgs e)
        {
            if (PreviewerLightingSettings == null) return;
            _visualizationType.Enabled = true;
            PreviewerLightingSettings.OverallType = PreviewerLightingSettings.LightingDelegateType.Utility;
            PreviewerLightingSettings_InvokeOnChange();
        }

        private void lightingRenderingButton_Click(object sender, EventArgs e)
        {
            if (PreviewerLightingSettings == null) return;
            _visualizationType.Enabled = false;
            PreviewerLightingSettings.MountedEnvSettings = "cfg/lighting_preview";
            PreviewerLightingSettings.OverallType = PreviewerLightingSettings.LightingDelegateType.Forward;
            PreviewerLightingSettings_InvokeOnChange();
        }

        private void _drawGrid_CheckedChanged(object sender, EventArgs e)
        {
            if (OverlaySettings == null) return;
            OverlaySettings.DrawGrid = OverlaySettings.DrawBasisAxis = _drawGrid.Checked;
            OverlaySettings_InvokeOnChange();
        }
    }
}
