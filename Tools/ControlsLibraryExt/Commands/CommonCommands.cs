// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

using System;
using System.ComponentModel.Composition;
using System.Windows.Forms;

using Sce.Atf;
using Sce.Atf.Applications;

#pragma warning disable 0649 // Field '...' is never assigned to, and will always have its default value null

namespace ControlsLibraryExt.Commands
{
    [Export(typeof(IInitializable))]
    [PartCreationPolicy(CreationPolicy.Shared)]
    public class CommonCommands : ICommandClient, IInitializable
    {
        public bool CanDoCommand(object commandTag)
        {
            if (!(commandTag is Command))
                return false;

            switch ((Command)commandTag)
            {
                case Command.SaveModifiedAssets:
                    return GUILayer.PendingSaveList.HasModifiedAssets()
                        || (_docRegistry != null && ModifiedDocumentPendingSaveList.HasModifiedDocuments(_docRegistry));

                case Command.ViewInvalidAssets:
                    return _assetTrackingService.InvalidAssetList.HasInvalidAssets();
            }

            return false;
        }

        public void DoCommand(object commandTag)
        {
            if (commandTag is Command)
            {
                switch ((Command)commandTag)
                {
                    case Command.SaveModifiedAssets:
                        PerformSaveModifiedAssets();
                        break;

                    case Command.ViewInvalidAssets:
                        PerformViewInvalidAssets();
                        break;
                }
            }
        }

        public void UpdateCommand(object commandTag, Sce.Atf.Applications.CommandState state)
        {
            // if (commandTag is Command)
            // {
            //     switch ((Command)commandTag)
            //     {
            //     }
            // }
        }

        public virtual void Initialize()
        {
            m_commandService.RegisterCommand(
                Command.SaveModifiedAssets,
                StandardMenu.File,
                "Assets",
                "Modified Assets...".Localize(),
                "View and save any assets that have been modified".Localize(),
                Keys.None,
                null,
                CommandVisibility.Menu,
                this);

            m_commandService.RegisterCommand(
                Command.ViewInvalidAssets,
                StandardMenu.File,
                "Assets",
                "View Invalid Assets...".Localize(),
                "View any assets at are marked as invalid".Localize(),
                Keys.None,
                null,
                CommandVisibility.Menu,
                this);
        }

        private enum Command
        {
            SaveModifiedAssets,
            ViewInvalidAssets
        }

        private void PerformSaveModifiedAssets()
        {
            using (var dialog = new ControlsLibrary.ModifiedAssetsDialog())
            {
                var pendingNativeAssetList = GUILayer.PendingSaveList.Create();
                Aga.Controls.Tree.ITreeModel assetList = new GUILayer.DivergentAssetList(
                    GUILayer.EngineDevice.GetInstance(), pendingNativeAssetList);

                ModifiedDocumentPendingSaveList pendingDocSaveList = null;
                if (_docRegistry != null && _docService != null)
                {
                    pendingDocSaveList = new ModifiedDocumentPendingSaveList(_docRegistry, _docService);
                    var docsList = new ModifiedDocumentList(_docRegistry, pendingDocSaveList);
                    var mergedList = new MergedDocumentList(new Aga.Controls.Tree.ITreeModel []{ assetList, docsList});
                    assetList = mergedList;
                }

                dialog.AssetList = assetList;

                if (dialog.ShowDialog() == DialogResult.OK)
                {
                    var cmtResult = pendingNativeAssetList.Commit();

                    // if we got some error messages during the commit; display them here...
                    if (!String.IsNullOrEmpty(cmtResult.ErrorMessages))
                        ControlsLibrary.BasicControls.TextWindow.ShowModal(cmtResult.ErrorMessages);

                    if (pendingDocSaveList != null)
                    {
                        cmtResult = pendingDocSaveList.Commit();
                        if (!String.IsNullOrEmpty(cmtResult.ErrorMessages))
                            ControlsLibrary.BasicControls.TextWindow.ShowModal(cmtResult.ErrorMessages);
                    }
                }
            }
        }

        private void PerformViewInvalidAssets()
        {
            using (var dialog = new ControlsLibrary.InvalidAssetDialog(_assetTrackingService.InvalidAssetList))
            {
                dialog.ShowDialog();
            }
        }

        [Import(AllowDefault = false)]
        private ICommandService m_commandService;

        [Import(AllowDefault = true)]
        private Sce.Atf.Applications.IDocumentRegistry _docRegistry;

        [Import(AllowDefault = true)]
        private Sce.Atf.Applications.IDocumentService _docService;

        [Import(AllowDefault = true)]
        private ControlsLibraryExt.AssetTrackingService _assetTrackingService;
    }
}
