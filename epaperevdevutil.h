#pragma once
#include <QString>
#include <QList>

// This code is originally from QEvdevUtil, extracted to keep this code independent.
namespace EpaperEvdevUtil {

struct ParsedSpecification
{
    QString spec;
    QList<QString> devices;
    QList<QString> args;
};

ParsedSpecification parseSpecification(const QString &specification);

}

