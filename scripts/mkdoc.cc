#include "lib/globals.h"
#include "lib/proto.h"
#include "lib/flags.h"
#include <fmt/format.h>

extern const std::map<std::string, std::string> formats;

static ConfigProto findConfig(std::string name)
{
    const auto data = formats.at(name);
    ConfigProto config;
    if (!config.ParseFromString(data))
        Error() << "bad config name: " + name;
    return config;
}

int main(int argc, const char* argv[])
{
	auto config = findConfig(argv[1]);

	fmt::print("# {}\n\n", config.comment());

	const auto& documentation = config.documentation();
	auto it = documentation.begin();
	if (it != documentation.end())
	{
		fmt::print("{}\n", *it++);

		fmt::print("## Options\n\n");

		if (!config.option().empty() && !config.option_group().empty())
		{
			for (const auto& option_group : config.option_group())
			{
				fmt::print("  - {}:\n", option_group.comment());
				for (const auto& option : option_group.option())
					fmt::print("      - `{}`: {}\n", option.name(), option.comment());
			}

			for (const auto& option : config.option())
				fmt::print("  - `{}`: {}\n", option.name(), option.comment());
		}
		else
			fmt::print("(no options)\n");
		fmt::print("\n");

		while (it != documentation.end())
			fmt::print("{}\n", *it++);
	}
	else
		fmt::print("(This format has no documentation. Please file a bug.)\n");

	return 0;
}

