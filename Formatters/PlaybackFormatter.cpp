#include "PlaybackFormatter.h"

namespace Formatters
{
	std::shared_ptr<FormatterRecording> CopyToRecording(TextInputFormatter<>& formatter)
	{
		auto result = std::make_shared<FormatterRecording>();

		unsigned subtreeEle = 0;
		bool t;
		TextInputFormatter<>::InteriorSection str;
		for (;;) {
			switch(formatter.PeekNext()) {
			case FormatterBlob::BeginElement:
				t = formatter.TryBeginElement();
				assert(t); (void)t;
				++subtreeEle;
				result->PushBeginElement();
				break;

			case FormatterBlob::EndElement:
				if (!subtreeEle) return result;    // end now, while the EndElement is primed

				t = formatter.TryEndElement();
				assert(t); (void)t;
				--subtreeEle;
				result->PushEndElement();
				break;

			case FormatterBlob::KeyedItem:
				t = formatter.TryKeyedItem(str);
				assert(t); (void)t;
				result->PushKeyedItem(str);
				break;

			case FormatterBlob::Value:
				t = formatter.TryStringValue(str);
				assert(t); (void)t;
				result->PushStringValue(str);
				break;

			case FormatterBlob::CharacterData:
				ThrowFormatException(formatter, "CharacterData not supported in formatter recording");
				break;

			default:
				ThrowFormatException(formatter, "Unexpected blob or end of stream hit while skipping forward");
			}
		}

		return result;
	}

	std::shared_ptr<IDynamicInputFormatter> PlaybackRecording(std::shared_ptr<FormatterRecording> recording, ::Assets::DependencyValidation depVal)
	{
		return std::make_shared<PlaybackFormatter>(std::move(recording), std::move(depVal));
	}
}