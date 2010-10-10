/* This file is part of Clementine.

   Clementine is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Clementine is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Clementine.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "songinfoprovider.h"
#include "songinfoview.h"
#include "ultimatelyricsprovider.h"
#include "ultimatelyricsreader.h"

#include <QFuture>
#include <QFutureWatcher>
#include <QSettings>
#include <QtConcurrentRun>

const char* SongInfoView::kSettingsGroup = "SongInfo";

typedef QList<SongInfoProvider*> ProviderList;

SongInfoView::SongInfoView(NetworkAccessManager* network, QWidget* parent)
  : SongInfoBase(network, parent),
    ultimate_reader_(new UltimateLyricsReader(network))
{
  // Parse the ultimate lyrics xml file in the background
  QFuture<ProviderList> future = QtConcurrent::run(
      ultimate_reader_.get(), &UltimateLyricsReader::Parse,
      QString(":lyrics/ultimate_providers.xml"));
  QFutureWatcher<ProviderList>* watcher = new QFutureWatcher<ProviderList>(this);
  watcher->setFuture(future);
  connect(watcher, SIGNAL(finished()), SLOT(UltimateLyricsParsed()));
}

SongInfoView::~SongInfoView() {
}

void SongInfoView::UltimateLyricsParsed() {
  QFutureWatcher<ProviderList>* watcher =
      static_cast<QFutureWatcher<ProviderList>*>(sender());

  foreach (SongInfoProvider* provider, watcher->result()) {
    fetcher_->AddProvider(provider);
  }

  watcher->deleteLater();
  ultimate_reader_.reset();

  ReloadSettings();
}

void SongInfoView::ResultReady(int id, const SongInfoFetcher::Result& result) {
  if (id != current_request_id_)
    return;

  Clear();

  foreach (const CollapsibleInfoPane::Data& data, result.info_) {
    AddSection(new CollapsibleInfoPane(data, this));
  }
}

void SongInfoView::ReloadSettings() {
  QSettings s;
  s.beginGroup(kSettingsGroup);

  // Put the providers in the right order
  QList<SongInfoProvider*> ordered_providers;

  QVariant saved_order = s.value("search_order");
  if (saved_order.isNull()) {
    // Hardcoded default order
    ordered_providers << ProviderByName("lyrics.wikia.com")
                      << ProviderByName("lyricstime.com")
                      << ProviderByName("lyricsreg.com")
                      << ProviderByName("lyricsmania.com")
                      << ProviderByName("metrolyrics.com")
                      << ProviderByName("seeklyrics.com")
                      << ProviderByName("azlyrics.com")
                      << ProviderByName("mp3lyrics.org")
                      << ProviderByName("songlyrics.com")
                      << ProviderByName("lyricsmode.com")
                      << ProviderByName("elyrics.net")
                      << ProviderByName("lyricsdownload.com")
                      << ProviderByName("lyrics.com")
                      << ProviderByName("lyricsbay.com")
                      << ProviderByName("directlyrics.com")
                      << ProviderByName("loudson.gs")
                      << ProviderByName("teksty.org")
                      << ProviderByName("tekstowo.pl (Polish translations)")
                      << ProviderByName("vagalume.uol.com.br")
                      << ProviderByName("vagalume.uol.com.br (Portuguese translations)");
  } else {
    foreach (const QVariant& name, saved_order.toList()) {
      SongInfoProvider* provider = ProviderByName(name.toString());
      if (provider)
        ordered_providers << provider;
    }
  }

  // Enable all the providers in the list and rank them
  int relevance = ordered_providers.count();
  foreach (SongInfoProvider* provider, ordered_providers) {
    provider->set_enabled(true);
    qobject_cast<UltimateLyricsProvider*>(provider)->set_relevance(relevance--);
  }

  // Any lyric providers we don't have in ordered_providers are considered disabled
  foreach (SongInfoProvider* provider, fetcher_->providers()) {
    if (qobject_cast<UltimateLyricsProvider*>(provider) && !ordered_providers.contains(provider)) {
      provider->set_enabled(false);
    }
  }
}

SongInfoProvider* SongInfoView::ProviderByName(const QString& name) const {
  foreach (SongInfoProvider* provider, fetcher_->providers()) {
    if (UltimateLyricsProvider* lyrics = qobject_cast<UltimateLyricsProvider*>(provider)) {
      if (lyrics->name() == name)
        return provider;
    }
  }
  return NULL;
}

namespace {
  bool CompareLyricProviders(const UltimateLyricsProvider* a, const UltimateLyricsProvider* b) {
    if (a->is_enabled() && !b->is_enabled())
      return true;
    if (!a->is_enabled() && b->is_enabled())
      return false;
    return a->relevance() > b->relevance();
  }
}

QList<const UltimateLyricsProvider*> SongInfoView::lyric_providers() const {
  QList<const UltimateLyricsProvider*> ret;
  foreach (SongInfoProvider* provider, fetcher_->providers()) {
    if (UltimateLyricsProvider* lyrics = qobject_cast<UltimateLyricsProvider*>(provider)) {
      ret << lyrics;
    }
  }
  qSort(ret.begin(), ret.end(), CompareLyricProviders);
  return ret;
}