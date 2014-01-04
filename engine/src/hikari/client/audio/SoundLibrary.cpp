#include "hikari/client/audio/SoundLibrary.hpp"
#include "hikari/client/audio/NSFSoundStream.hpp"
#include "hikari/core/util/FileSystem.hpp"
#include "hikari/core/util/Log.hpp"

#include <algorithm>
#include <functional>
#include <string>

#include <json/reader.h>
#include <json/value.h>

namespace hikari {

    const unsigned int SoundLibrary::MUSIC_BUFFER_SIZE = 2048 * 2;  // some platforms need larger buffer
    const unsigned int SoundLibrary::SAMPLE_BUFFER_SIZE = 2048 * 2; // so we'll double it for now.
    const unsigned int SoundLibrary::AUDIO_SAMPLE_RATE = 44000;

    SoundLibrary::SoundLibrary(const std::string & file)
        : isEnabledFlag(false)
        , file(file)
        , music()
        , samples()
        , samplers()
        , currentlyPlayingSample(nullptr) {
        loadLibrary();
    }

    void SoundLibrary::loadLibrary() {
        const std::string PROP_FILE     = "file";
        const std::string PROP_TITLE    = "title";
        const std::string PROP_MUSIC    = "music";
        const std::string PROP_SAMPLES  = "samples";
        const std::string PROP_TRACK    = "track";
        const std::string PROP_NAME     = "name";
        const std::string PROP_PRIORITY = "priority";
        Json::Reader reader;
        Json::Value root;
        auto fileContents = FileSystem::openFileRead(file);

        if(reader.parse(*fileContents, root, false)) {
            auto nsfCount = root.size();
            
            for(decltype(nsfCount) samplerIndex = 0; samplerIndex < nsfCount; ++samplerIndex) {
                const Json::Value & currentLibrary = root[samplerIndex];
                const std::string nsfFile = currentLibrary[PROP_FILE].asString();

                //
                // Create the sound stream/emulators
                //
                auto musicStream = std::make_shared<NSFSoundStream>(MUSIC_BUFFER_SIZE, 1);
                auto sampleStream = std::make_shared<NSFSoundStream>(SAMPLE_BUFFER_SIZE, 1);
                musicStream->open(nsfFile);
                sampleStream->open(nsfFile);

                SamplerPair samplerPair;
                samplerPair.musicStream = musicStream;
                samplerPair.sampleStream = sampleStream;

                samplers.push_back(samplerPair);
                HIKARI_LOG(debug) << "Loaded sampler for " << nsfFile;

                //
                // Parse the music
                //
                const Json::Value & musicArray = currentLibrary[PROP_MUSIC];
                auto musicCount = musicArray.size();

                for(decltype(musicCount) musicIndex = 0; musicIndex < musicCount; ++musicIndex) {
                    const Json::Value & musicEntryJson = musicArray[musicIndex];
                    const std::string & name = musicEntryJson[PROP_NAME].asString();

                    auto musicEntry = std::make_shared<MusicEntry>();
                    musicEntry->track = musicEntryJson[PROP_TRACK].asUInt();
                    musicEntry->samplerId = samplerIndex;

                    music.insert(std::make_pair(name, musicEntry));
                    HIKARI_LOG(debug) << "-> loaded music \"" << name << "\"";
                }

                //
                // Parse the samples
                //
                const Json::Value & sampleArray = currentLibrary[PROP_SAMPLES];
                auto sampleCount = sampleArray.size();

                for(decltype(sampleCount) sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
                    const Json::Value & sampleEntryJson = sampleArray[sampleIndex];
                    const std::string & name = sampleEntryJson[PROP_NAME].asString();

                    auto sampleEntry = std::make_shared<SampleEntry>();
                    sampleEntry->track = sampleEntryJson[PROP_TRACK].asUInt();
                    sampleEntry->priority = sampleEntryJson[PROP_PRIORITY].asUInt();
                    sampleEntry->samplerId = samplerIndex;

                    samples.insert(std::make_pair(name, sampleEntry));
                    HIKARI_LOG(debug) << "-> loaded sample \"" << name << "\"";

                }
            }
        }

        isEnabledFlag = true;
    }

    bool SoundLibrary::isEnabled() const {
        return isEnabledFlag;
    }

    void SoundLibrary::addMusic(const std::string & name, std::shared_ptr<MusicEntry> entry) {
        music.insert(std::make_pair(name, entry));
    }

    void SoundLibrary::addSample(const std::string & name, std::shared_ptr<SampleEntry> entry) {
        samples.insert(std::make_pair(name, entry));
    }

    std::shared_ptr<NSFSoundStream> SoundLibrary::playMusic(const std::string & name) {
        const auto & iterator = music.find(name);

        if(iterator != std::end(music)) {
            const std::shared_ptr<MusicEntry> & musicEntry = (*iterator).second;
            const SamplerPair & samplerPair = samplers.at(musicEntry->samplerId);
            const auto & stream = samplerPair.musicStream;

            stopMusic();
            stream->setCurrentTrack(musicEntry->track);
            stream->play();

            return stream;
        }

        return std::shared_ptr<NSFSoundStream>(nullptr);
    }

    std::shared_ptr<NSFSoundStream> SoundLibrary::playSample(const std::string & name) {
        const auto & iterator = samples.find(name);

        if(iterator != std::end(samples)) {
            const std::shared_ptr<SampleEntry> & sampleEntry = (*iterator).second;
            const SamplerPair & samplerPair = samplers.at(sampleEntry->samplerId);
            const auto & stream = samplerPair.sampleStream;

            // If a sample is currently playing, check to see if we should
            // interrupt it or not. If it's not playing then don't bother.
            
            
            // This needs to be worked out a little bit more.

            if(currentlyPlayingSample) {
                const SamplerPair & currentlyPlayingSamplerPair = samplers.at(currentlyPlayingSample->samplerId);
                const auto & currentlyPlayingStream = currentlyPlayingSamplerPair.sampleStream;

                if(currentlyPlayingStream->getStatus() == sf::SoundStream::Playing) {
                    // if(currentlyPlayingSample == sampleEntry) {
                    //     stopSample();
                    //     stream->setCurrentTrack(sampleEntry->track);
                    //     stream->play();

                    //     currentlyPlayingSample = sampleEntry;

                    //     return stream;
                    // }

                    if(sampleEntry->priority < currentlyPlayingSample->priority) {
                        // We're trying to play a sample with lower priority so just bail out.
                        return std::shared_ptr<NSFSoundStream>(nullptr);
                    }
                }
            }
            

            stopSample();
            stream->setCurrentTrack(sampleEntry->track);
            stream->play();

            currentlyPlayingSample = sampleEntry;

            return stream;
        }

        return std::shared_ptr<NSFSoundStream>(nullptr);
    }

    void SoundLibrary::stopMusic() {
        std::for_each(std::begin(samplers), std::end(samplers), [](SamplerPair & sampler) {
            if(const auto & musicSampler = sampler.musicStream) {
                musicSampler->stop();
            }
        });
    }

    void SoundLibrary::stopSample() {
        std::for_each(std::begin(samplers), std::end(samplers), [](SamplerPair & sampler) {
            if(const auto & sampleSampler = sampler.sampleStream) {
                sampleSampler->stopAllSamplers();
                sampleSampler->stop();
            }
        });
    }

} // hikari
