#include "util.h"

#include <iomanip>
#include <ctime>
#include <sstream>
#include <optional>

namespace LM
{
    std::filesystem::path DefaultSaveFolder()
    {
        std::filesystem::path fullPath;

#ifdef _WIN32
        // std::getenv returns nullptr when the variable is unset —
        // constructing a path from nullptr is undefined behavior (crash).
        // Guard every lookup and fall back to the executable folder.
        const char* homeDrive = std::getenv("HOMEDRIVE");
        const char* homePath = std::getenv("HOMEPATH");
        if (homeDrive && *homeDrive && homePath && *homePath)
        {
            fullPath = homeDrive;
            fullPath /= homePath;
            fullPath /= "LaneMakerData";
        }
        else
        {
            const char* userProfile = std::getenv("USERPROFILE");
            if (userProfile && *userProfile)
            {
                fullPath = userProfile;
                fullPath /= "LaneMakerData";
            }
        }
#elif __linux__
        const char* home = std::getenv("HOME");
        if (home && *home)
        {
            fullPath = home;
            fullPath /= "LaneMakerData";
        }
#else
#endif
        if (fullPath.empty())
        {
            // Final fallback: current working directory
            fullPath = ".";
            fullPath /= "LaneMakerData";
        }
        bool success = true;
        try
        {
            std::filesystem::create_directories(fullPath);
        }
        catch (const std::filesystem::filesystem_error&)
        {
            success = false;
        }

        if (!success)
        {
            // Fallback to executable folder
            fullPath = "";
            std::error_code ec;
            std::filesystem::create_directories(fullPath, ec);
        }

        return fullPath;
    }

    std::string CurrentDateTime()
    {
        auto t = std::time(nullptr);
        auto tm = *std::localtime(&t);

        std::ostringstream oss;
        oss << std::put_time(&tm, "%m-%d_%H-%M-%S");
        return oss.str();
    }

    namespace
    {
        std::optional<std::string> runTimestamp;
    }

    std::string RunTimestamp()
    {
        if (!runTimestamp.has_value())
        {
            runTimestamp.emplace(CurrentDateTime());
        }
        return runTimestamp.value();
    }

    TQDM<std::vector<size_t>>::HelperRange range(size_t s)
    {
        std::vector<size_t> rtn(s);
        for (int i = 0; i != s; ++i)
        {
            rtn[i] = i;
        }
        return rtn;
    }

    QString ExtractResourceToTempFile(const QString& resourcePath)
    {
        QFile resourceFile(resourcePath);
        if (!resourceFile.open(QIODevice::ReadOnly)) {
            return QString();
        }

        QTemporaryFile tempFile;
        tempFile.setAutoRemove(false);  // Keep the file after closing
        if (tempFile.open()) {
            tempFile.write(resourceFile.readAll());
            tempFile.close();
            return tempFile.fileName();
        }

        return QString();
    }
}
