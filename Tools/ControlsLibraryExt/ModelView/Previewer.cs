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
using System.Runtime.InteropServices;

using Sce.Atf;
using Sce.Atf.Adaptation;
using Sce.Atf.Applications;
using Sce.Atf.Controls;
using Sce.Atf.Controls.Adaptable;

#pragma warning disable 0649        // Field '...' is never assigned to, and will always have its default value null

namespace ControlsLibraryExt.ModelView
{
    [Export(typeof(IControlHostClient))]
    [Export(typeof(Previewer))]
    [Export(typeof(IInitializable))]
    [PartCreationPolicy(CreationPolicy.Shared)]
    public class Previewer : IControlHostClient, IInitializable
    {
        [ImportingConstructor]
        public Previewer(
            IControlHostService controlHostService,
            ICommandService commandService,
            IContextRegistry contextRegistry)
        {
            m_controlHostService = controlHostService;
            m_commandService = commandService;
            m_contextRegistry = contextRegistry;
        }

        private IControlHostService m_controlHostService;
        private ICommandService m_commandService;
        private IContextRegistry m_contextRegistry;

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

                // attach tweakable bridge for accessing console variables
                m_scriptingService.SetVariable("cv", new GUILayer.TweakableBridge());
            }
        }

        #endregion

        #region IControlHostClient Members

        public void Activate(Control control)
        {
            AdaptableControl adaptableControl = (AdaptableControl)control;
            if (adaptableControl != null)
            {
                m_contextRegistry.ActiveContext = adaptableControl.Context;
            }
        }

        public void Deactivate(Control control) { }

        public bool Close(Control control)
        {
            return true;
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

        private HoverBase _hoverForm;

        [Import]
        private ExportProvider _exportProvider;

        [Import]
        private ControlsLibraryExt.Material.ActiveMaterialContext _activeMaterialContext;

        public ControlsLibraryExt.ModelView.PreviewerContext OpenPreviewWindow()
        {
            var context = _exportProvider.GetExport<ControlsLibraryExt.ModelView.PreviewerContext>().Value;
            var control = _exportProvider.GetExport<ControlsLibraryExt.ModelView.PreviewerControl>().Value;
            control.ActiveMaterialContext = _activeMaterialContext;
            control.SetContext(context);
            _exportProvider.GetExport<IControlHostService>().Value.RegisterControl(
                control.As<Control>(),
                new ControlInfo("Previewer", "Prewiewer", StandardControlGroup.Center),
                this);
            return context;
        }
    }

    [Export(typeof(IInitializable))]
    [PartCreationPolicy(CreationPolicy.Shared)]
    public class GlobalPreviewerCommands : ICommandClient, IInitializable
    {
        public bool CanDoCommand(object commandTag)
        {
            if (!(commandTag is Command))
                return false;

            switch ((Command)commandTag)
            {
                case Command.NewPreviewWindow:
                    return true;
            }

            return false;
        }

        public void DoCommand(object commandTag)
        {
            if (commandTag is Command)
            {
                switch ((Command)commandTag)
                {
                    case Command.NewPreviewWindow:
                        _controlHost.OpenPreviewWindow();
                        break;
                }
            }
        }

        public void UpdateCommand(object commandTag, CommandState state) { }

        public virtual void Initialize()
        {
            _commandService.RegisterCommand(
                new CommandInfo(
                    Command.NewPreviewWindow,
                    StandardMenu.File,
                    "Previewer",
                    "New Preview Window".Localize(),
                    "Open a new previewer window".Localize(),
                    Sce.Atf.Input.Keys.None,
                    null,
                    CommandVisibility.Menu),
                this);
        }

        private enum Command
        {
            NewPreviewWindow
        }

        [Import(AllowDefault = false)] private ICommandService _commandService;
        [Import(AllowDefault = false)] private Previewer _controlHost;
    }

    [Export(typeof(IInitializable))]
    [PartCreationPolicy(CreationPolicy.Shared)]
    public class PreviewerCommands : ICommandClient, IInitializable
    {
        public bool CanDoCommand(object commandTag)
        {
            if (!(commandTag is Command))
                return false;

            switch ((Command)commandTag)
            {
                case Command.SelectScene:
                case Command.SelectAnimationSet:
                    return _contextRegistry.GetActiveContext<PreviewerContext>() != null;
            }

            return false;
        }

        public void DoCommand(object commandTag)
        {
            if (commandTag is Command)
            {
                var context = _contextRegistry.GetActiveContext<PreviewerContext>();
                if (context != null)
                {
                    switch ((Command)commandTag)
                    {
                        case Command.SelectScene:
                            {
                                var controls = new ControlsLibraryExt.SceneSelector.SelectPreviewSceneModal(context);
                                controls.ShowDialog();
                            }
                            break;

                        case Command.SelectAnimationSet:
                            if (OpenFilesWithFilters(_ofd_Animation, GUILayer.Utils.GetAnimationSetExtensions()) == DialogResult.OK)
                            {
                                var settings = context.ModelSettings;
                                settings.AnimationFileName = _ofd_Animation.FileName;
                                context.ModelSettings = settings;
                            }
                            break;
                    }
                }
            }
        }

        private DialogResult OpenFilesWithFilters(OpenFileDialog ofd, System.Collections.Generic.IEnumerable<GUILayer.Utils.AssetExtension> filters)
        {
            var sb = new System.Text.StringBuilder("", 256);
            bool first = true;
            bool folderIsAnOption = false;
            foreach (var f in filters)
            {
                if (f.Extension == "folder")
                {
                    folderIsAnOption = true;
                    continue;
                }

                if (!first) sb.Append("|");
                first = false;
                sb.Append(f.Description);
                sb.Append("|*.");
                sb.Append(f.Extension);
            }
            if (!first) sb.Append("|");
            first = false;
            sb.Append("All Files|*.*");

            ofd.Filter = sb.ToString();
            return ofd.ShowDialog();
        }

        public void UpdateCommand(object commandTag, CommandState state) { }

        public virtual void Initialize()
        {
            _commandService.RegisterMenu(
                new MenuInfo(
                    Command.PreviewerConfigMenu,
                    "Previewer",
                    "Previewer configuration"));

            _commandService.RegisterCommand(
                new CommandInfo(
                    Command.SelectScene,
                    Command.PreviewerConfigMenu,
                    null,
                    "Select Scene".Localize(),
                    "Select scene to display in previewer".Localize(),
                    Sce.Atf.Input.Keys.None,
                    null,
                    CommandVisibility.Menu),
                this);

            _commandService.RegisterCommand(
                new CommandInfo(
                    Command.SelectAnimationSet,
                    Command.PreviewerConfigMenu,
                    null,
                    "Select Animation Set".Localize(),
                    "Select animation set to use in previewer".Localize(),
                    Sce.Atf.Input.Keys.None,
                    null,
                    CommandVisibility.Menu),
                this);
        }

        private enum Command
        {
            PreviewerConfigMenu,
            SelectScene,
            SelectAnimationSet
        }

        [Import(AllowDefault = false)]
        private ICommandService _commandService;

        [Import(AllowDefault = false)]
        private IContextRegistry _contextRegistry;

        private OpenFileDialog _ofd_Skin = new OpenFileDialog();
        private OpenFileDialog _ofd_Animation = new OpenFileDialog();
        private OpenFileDialog _ofd_Skeleton = new OpenFileDialog();
    }
}