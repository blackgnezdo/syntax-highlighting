/*
    Copyright (C) 2016 Volker Krause <vkrause@kde.org>

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU Library General Public License as published by
    the Free Software Foundation; either version 2 of the License, or (at your
    option) any later version.

    This program is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Library General Public
    License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "definition.h"
#include "definitionref_p.h"
#include "context.h"
#include "rule.h"
#include "format.h"

#include <QDebug>
#include <QFile>
#include <QHash>
#include <QVector>
#include <QXmlStreamReader>

#include <algorithm>

using namespace SyntaxHighlighting;

namespace SyntaxHighlighting {
class DefinitionPrivate
{
public:
    DefinitionPrivate();
    ~DefinitionPrivate();

    bool isLoaded() const;
    void loadLanguage(QXmlStreamReader &reader);
    void loadHighlighting(Definition *def, QXmlStreamReader &reader);
    void loadContexts(Definition *def, QXmlStreamReader &reader);
    void loadItemData(QXmlStreamReader &reader);
    void loadGeneral(QXmlStreamReader &reader);

    Repository *repo;
    QHash<QString, KeywordList> keywordLists;
    QVector<Context*> contexts;
    QHash<QString, Format> formats;
    QString delimiters;

    QString fileName;
    QString name;
    QString section;
    QVector<QString> extensions;
    QVector<QString> mimetypes;
    Qt::CaseSensitivity caseSensitive;
    float version;
    bool hidden;
};
}

DefinitionPrivate::DefinitionPrivate() :
    repo(nullptr),
    delimiters(QStringLiteral(".():!+,-<=>%&*/;?[]^{|}~\\ \t")),
    caseSensitive(Qt::CaseSensitive),
    version(0.0f),
    hidden(false)
{
}

DefinitionPrivate::~DefinitionPrivate()
{
    qDeleteAll(contexts);
}

Definition::Definition() :
    d(new DefinitionPrivate)
{
}

Definition::Definition(const Definition &other) :
    d(other.d)
{
}

Definition::~Definition()
{
}

Definition& Definition::operator=(const Definition &rhs)
{
    d = rhs.d;
    return *this;
}

bool Definition::isValid() const
{
    return d->repo && !d->fileName.isEmpty();
}

Repository* Definition::repository() const
{
    return d->repo;
}

void Definition::setRepository(Repository* repo)
{
    d->repo = repo;
}

QString Definition::name() const
{
    return d->name;
}

QVector<QString> Definition::extensions() const
{
    return d->extensions;
}

float Definition::version() const
{
    return d->version;
}

Context* Definition::initialContext() const
{
    Q_ASSERT(!d->contexts.isEmpty());
    return d->contexts.first();
}

Context* Definition::contextByName(const QString& name) const
{
    foreach (auto context, d->contexts) {
        if (context->name() == name)
            return context;
    }
    return nullptr;
}

KeywordList Definition::keywordList(const QString& name) const
{
    return d->keywordLists.value(name);
}

bool Definition::isDelimiter(QChar c) const
{
    return d->delimiters.contains(c);
}

Format Definition::formatByName(const QString& name) const
{
    const auto it = d->formats.constFind(name);
    if (it != d->formats.constEnd())
        return it.value();

    qWarning() << "Unknown format" << name << "in" << d->name;
    return Format();
}

bool DefinitionPrivate::isLoaded() const
{
    return !contexts.isEmpty();
}

bool Definition::load()
{
    if (d->isLoaded())
        return true;

    Q_ASSERT(!d->fileName.isEmpty());
    QFile file(d->fileName);
    if (!file.open(QFile::ReadOnly))
        return false;

    QXmlStreamReader reader(&file);
    while (!reader.atEnd()) {
        const auto token = reader.readNext();
        if (token != QXmlStreamReader::StartElement)
            continue;

        if (reader.name() == QLatin1String("highlighting"))
            d->loadHighlighting(this, reader);

        else if (reader.name() == QLatin1String("general"))
            d->loadGeneral(reader);
    }

    for (auto it = d->keywordLists.begin(); it != d->keywordLists.end(); ++it)
        (*it).setCaseSensitivity(d->caseSensitive);

    foreach (auto context, d->contexts) {
        context->resolveContexts();
        context->resolveIncludes();
    }

    return true;
}

bool Definition::loadMetaData(const QString& definitionFileName)
{
    d->fileName = definitionFileName;

    QFile file(definitionFileName);
    if (!file.open(QFile::ReadOnly))
        return false;

    QXmlStreamReader reader(&file);
    while (!reader.atEnd()) {
        const auto token = reader.readNext();
        if (token != QXmlStreamReader::StartElement)
            continue;
        if (reader.name() == QLatin1String("language")) {
            d->loadLanguage(reader);
            return true;
        }
    }

    return false;
}

void DefinitionPrivate::loadLanguage(QXmlStreamReader &reader)
{
    Q_ASSERT(reader.name() == QLatin1String("language"));
    Q_ASSERT(reader.tokenType() == QXmlStreamReader::StartElement);

    name = reader.attributes().value(QStringLiteral("name")).toString();
    section = reader.attributes().value(QStringLiteral("section")).toString();
    version = reader.attributes().value(QStringLiteral("version")).toFloat();
    hidden = reader.attributes().value(QStringLiteral("hidden")) == QLatin1String("true");
    const auto exts = reader.attributes().value(QStringLiteral("extensions")).toString();
    foreach (const auto &ext, exts.split(QLatin1Char(';'), QString::SkipEmptyParts)) {
        if (ext.startsWith(QLatin1String("*.")))
            extensions.push_back(ext.mid(2));
        else
            extensions.push_back(ext);
    }
    const auto mts = reader.attributes().value(QStringLiteral("mimetypes")).toString();
    foreach (const auto &mt, mts.split(QLatin1Char(';'), QString::SkipEmptyParts))
        mimetypes.push_back(mt);
}

void DefinitionPrivate::loadHighlighting(Definition *def, QXmlStreamReader& reader)
{
    Q_ASSERT(reader.name() == QLatin1String("highlighting"));
    Q_ASSERT(reader.tokenType() == QXmlStreamReader::StartElement);

    while (!reader.atEnd()) {
        switch (reader.tokenType()) {
            case QXmlStreamReader::StartElement:
                if (reader.name() == QLatin1String("list")) {
                    KeywordList keywords;
                    keywords.load(reader);
                    keywordLists.insert(keywords.name(), keywords);
                } else if (reader.name() == QLatin1String("contexts")) {
                    loadContexts(def, reader);
                    reader.readNext();
                } else if (reader.name() == QLatin1String("itemDatas")) {
                    loadItemData(reader);
                } else {
                    reader.readNext();
                }
                break;
            case QXmlStreamReader::EndElement:
                return;
            default:
                reader.readNext();
                break;
        }
    }
}

void DefinitionPrivate::loadContexts(Definition *def, QXmlStreamReader& reader)
{
    Q_ASSERT(reader.name() == QLatin1String("contexts"));
    Q_ASSERT(reader.tokenType() == QXmlStreamReader::StartElement);

    while (!reader.atEnd()) {
        switch (reader.tokenType()) {
            case QXmlStreamReader::StartElement:
                if (reader.name() == QLatin1String("context")) {
                    auto context = new Context;
                    context->setDefinition(*def);
                    context->load(reader);
                    contexts.push_back(context);
                }
                reader.readNext();
                break;
            case QXmlStreamReader::EndElement:
                return;
            default:
                reader.readNext();
                break;
        }
    }
}

void DefinitionPrivate::loadItemData(QXmlStreamReader& reader)
{
    Q_ASSERT(reader.name() == QLatin1String("itemDatas"));
    Q_ASSERT(reader.tokenType() == QXmlStreamReader::StartElement);

    while (!reader.atEnd()) {
        switch (reader.tokenType()) {
            case QXmlStreamReader::StartElement:
                if (reader.name() == QLatin1String("itemData")) {
                    Format f;
                    f.load(reader);
                    formats.insert(f.name(), f);
                    reader.readNext();
                }
                reader.readNext();
                break;
            case QXmlStreamReader::EndElement:
                return;
            default:
                reader.readNext();
                break;
        }
    }
}

static bool attrToBool(const QStringRef &str)
{
    return str == QLatin1String("1") || str == QLatin1String("true");
}

void DefinitionPrivate::loadGeneral(QXmlStreamReader& reader)
{
    Q_ASSERT(reader.name() == QLatin1String("general"));
    Q_ASSERT(reader.tokenType() == QXmlStreamReader::StartElement);
    reader.readNext();

    while (!reader.atEnd()) {
        switch (reader.tokenType()) {
            case QXmlStreamReader::StartElement:
                if (reader.name() == QLatin1String("keywords")) {
                    caseSensitive = attrToBool(reader.attributes().value(QStringLiteral("casesensitive"))) ? Qt::CaseSensitive : Qt::CaseInsensitive;
                    delimiters += reader.attributes().value(QStringLiteral("additionalDeliminator"));
                    std::sort(delimiters.begin(), delimiters.end());
                    auto it = std::unique(delimiters.begin(), delimiters.end());
                    delimiters.truncate(std::distance(delimiters.begin(), it));
                    foreach (const auto c, reader.attributes().value(QLatin1String("weakDeliminator")))
                        delimiters.remove(c);
                } else {
                    reader.skipCurrentElement();
                }
                reader.readNext();
                break;
            case QXmlStreamReader::EndElement:
                return;
            default:
                reader.readNext();
                break;
        }
    }
}

DefinitionRef::DefinitionRef()
{
}

DefinitionRef::DefinitionRef(const Definition &def) :
    d(def.d)
{
}

DefinitionRef::~DefinitionRef()
{
}

DefinitionRef& DefinitionRef::operator=(const Definition &def)
{
    d = def.d;
    return *this;
}

Definition DefinitionRef::definition() const
{
    Definition def;
    if (!d.expired())
        def.d = d.lock();
    return def;
}