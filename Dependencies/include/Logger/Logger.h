#ifndef LOGGER_H_DEFINED
#define LOGGER_H_DEFINED

class Logger
{
public:

	enum Verbosity : unsigned char
	{
		ERROR,
		WARNING,
		MESSAGE,
		TRIVIAL
	};

	static void setVerbosity(Verbosity verbosity);

	static void logMessage(const char *message);
	static void logMessageFormatted(const char *format, ...);
	
	static void logError(const char *error);
	static void logErrorFormatted(const char *format, ...);

	static void logWarning(const char *message);
	static void logWarningFormatted(const char *format, ...);

	static void logTrivial(const char *message);
	static void logTrivialFormatted(const char *format, ...);

private:

	static Verbosity verbosity;


	Logger() = default;
};


#endif
