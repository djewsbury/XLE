// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "AssetsLegacy.h"
#include "../../OSServices/RawFS.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../Utility/Threading/Mutex.h"

namespace Assets { namespace Legacy
{
    static const SplitPath<ResChar>& BaseDir()
    {
            // hack -- find the base directory we'll use to build relative names from 
            //  Note that this is going to call GetCurrentDirectory at some unpredictable
            //  time; so we're assuming that the current directory is set at app start, and
            //  remains constant
        static ResolvedAssetFile buffer;
        static SplitPath<ResChar> result;
        static bool init = false;
        if (!init) {
			static Threading::Mutex lock;
			ScopedLock(lock);
            OSServices::GetCurrentDirectory(dimof(buffer._fn), buffer._fn);
            SplitPath<ResChar>(buffer._fn).Rebuild(buffer._fn, dimof(buffer._fn));
            result = SplitPath<ResChar>(buffer._fn);
            init = true;
        }
        return result;
    }

    void MakeAssetName(ResolvedAssetFile& dest, StringSection<ResChar> src)
    {
        auto rules = s_defaultFilenameRules;
        FileNameSplitter<ResChar> srcSplit(src);
        auto relPath = 
            MakeRelativePath(
                BaseDir(),
                SplitPath<ResChar>(srcSplit.StemAndPath()),
                rules);

        XlCopyString(dest._fn, relPath.c_str());

        auto* i = XlStringEnd(dest._fn);
        auto* iend = ArrayEnd(dest._fn);
        auto fileAndExtension = srcSplit.FileAndExtension();
        auto* s = fileAndExtension._start;
        auto* send = fileAndExtension._end;

        while (i!=iend && s!=send) { *i = ConvertPathChar(*s, rules); ++i; ++s; }
        
        if (!srcSplit.ParametersWithDivider().IsEmpty()) {
            s = srcSplit.ParametersWithDivider()._start;
            send = srcSplit.ParametersWithDivider()._end;
            while (i!=iend && s!=send) { *i = *s; ++i; ++s; }
        }

        *std::min(i, iend-1) = '\0';
    }
}}

