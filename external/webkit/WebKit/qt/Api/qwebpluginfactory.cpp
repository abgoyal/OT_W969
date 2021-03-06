

#include "config.h"
#include "qwebpluginfactory.h"








bool QWebPluginFactory::MimeType::operator==(const MimeType& other) const
{
    return name == other.name
           && description == other.description
           && fileExtensions == other.fileExtensions;
}





QWebPluginFactory::QWebPluginFactory(QObject *parent)
    : QObject(parent)
{
}

QWebPluginFactory::~QWebPluginFactory()
{
}


void QWebPluginFactory::refreshPlugins()
{
}





bool QWebPluginFactory::extension(Extension extension, const ExtensionOption *option, ExtensionReturn *output)
{
    Q_UNUSED(extension)
    Q_UNUSED(option)
    Q_UNUSED(output)
    return false;
}

bool QWebPluginFactory::supportsExtension(Extension extension) const
{
    Q_UNUSED(extension)
    return false;
}
