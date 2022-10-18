// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

using Sce.Atf;
using System.ComponentModel.Composition;

namespace ControlsLibraryExt
{
    [Export(typeof(IInitializable))]
    [Export(typeof(AssetTrackingService))]
    [PartCreationPolicy(CreationPolicy.Shared)]
    public class AssetTrackingService : IInitializable
    {
        public GUILayer.InvalidAssetList InvalidAssetList { get { return _invalidAssetList; } }

        void IInitializable.Initialize()
        {
            _invalidAssetList = new GUILayer.InvalidAssetList();
        }

        private GUILayer.InvalidAssetList _invalidAssetList;
    }
}
