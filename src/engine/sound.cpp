#include <algorithm>
#include <string>
#include <map>
using std::swap;
#include "engine.h"
#include "SDL_mixer.h"

struct soundsample
{
    Mix_Chunk* sound;
    char* name;

    soundsample() : name(nullptr) {}
    ~soundsample() { }

    void cleanup()
    {
        Mix_FreeChunk(sound);
        sound = nullptr;
    }
};

soundslot::soundslot() : vol(255), maxrad(-1), minrad(-1), name("") {}
soundslot::~soundslot() { }

sound::sound() : hook(nullptr) { reset(); }
sound::~sound() {}
bool sound::playing() { return chan >= 0 && (Mix_Playing(chan) || Mix_Paused(chan)); }
void sound::reset()
{
    pos = oldpos = vec(-1, -1, -1);
    slot = nullptr;
    owner = nullptr;
    vol = curvol = 255;
    curpan = 127;
    material = MAT_AIR;
    flags = maxrad = minrad = millis = ends = 0;
    slotnum = chan = -1;
    if (hook) *hook = -1;
    hook = nullptr;
    buffer.shrink(0);
}

hashnameset<soundsample> soundsamples;
std::map<const int, soundslot> gamesounds, mapsounds;
vector<sound> sounds;

bool nosound = true, changedvol = false, canmusic = false;
Mix_Music* music = nullptr;
SDL_RWops* musicrw = nullptr;
stream* musicstream = nullptr;
std::string musicfile = "", musicdonecmd = "";
int musictime = -1, musicdonetime = -1;

VARF(IDF_PERSIST, mastervol, 0, 255, 255, changedvol = true; if(!music && musicvol > 0 && mastervol > 0) smartmusic(true));
VAR(IDF_PERSIST, soundvol, 0, 255, 255);
VARF(0, soundmono, 0, 0, 1, initwarning("sound configuration", INIT_RESET, CHANGE_SOUND));
VARF(0, soundmixchans, 16, 32, 1024, initwarning("sound configuration", INIT_RESET, CHANGE_SOUND));
VARF(0, soundfreq, 0, 44100, 48000, initwarning("sound configuration", INIT_RESET, CHANGE_SOUND));
VARF(0, soundbuflen, 128, 4096, VAR_MAX, initwarning("sound configuration", INIT_RESET, CHANGE_SOUND));
VAR(IDF_PERSIST, soundmaxrad, 0, 512, VAR_MAX);
VAR(IDF_PERSIST, soundminrad, 0, 0, VAR_MAX);
FVAR(IDF_PERSIST, soundevtvol, 0, 1, FVAR_MAX);
FVAR(IDF_PERSIST, soundevtscale, 0, 1, FVAR_MAX);
FVAR(IDF_PERSIST, soundenvvol, 0, 1, FVAR_MAX);
FVAR(IDF_PERSIST, soundenvscale, 0, 1, FVAR_MAX);
VAR(IDF_PERSIST, soundcull, 0, 1, 1);

VARF(IDF_PERSIST, musicvol, 0, 64, 255, changedvol = true; if(!music && musicvol > 0) smartmusic(true));
VAR(IDF_PERSIST, musicfadein, 0, 1000, VAR_MAX);
VAR(IDF_PERSIST, musicfadeout, 0, 2500, VAR_MAX);
SVAR(0, titlemusic, "sounds/theme");

void initsound()
{
    if(nosound)
    {
        SDL_version version;
        SDL_GetVersion(&version);
        if(version.major == 2 && version.minor == 0 && version.patch == 6)
        {
            conoutf("\frSound is broken in SDL 2.0.6");
            return;
        }
        if(Mix_OpenAudio(soundfreq, MIX_DEFAULT_FORMAT, soundmono ? 1 : 2, soundbuflen) == -1)
        {
            conoutf("\frsound initialisation failed: %s", Mix_GetError());
            return;
        }
        int chans = Mix_AllocateChannels(soundmixchans);
        conoutf("allocated %d of %d sound channels", chans, soundmixchans);
        nosound = false;
    }
    initmumble();
}

void stopmusic(bool docmd)
{
    if(nosound) return;
    if(Mix_PlayingMusic()) Mix_HaltMusic();
    if(music)
    {
        Mix_FreeMusic(music);
        music = nullptr;
    }
    if(musicrw != nullptr) { SDL_FreeRW(musicrw); musicrw = nullptr; }
    DELETEP(musicstream);
    musicfile = "";
    if(!musicdonecmd.empty())
    {
        std::string cmd = musicdonecmd;
        musicdonecmd = "";
        if(docmd) execute(cmd.c_str());
    }
    musicdonetime = -1;
}

void musicdone(bool docmd)
{
    if(nosound) return;
    if(musicfadeout && !docmd)
    {
        if(Mix_PlayingMusic()) Mix_FadeOutMusic(musicfadeout);
    }
    else stopmusic(docmd);
}

void stopsound()
{
    if(nosound) return;
    Mix_HaltChannel(-1);
    stopmusic(false);
    clearsound();
    enumerate(soundsamples, soundsample, s, s.cleanup());
    soundsamples.clear();
    gamesounds.clear();
    closemumble();
    Mix_CloseAudio();
    nosound = true;
}

void removesound(int c)
{
    if(!nosound) Mix_HaltChannel(c);
    sounds[c].reset();
}

void clearsound()
{
    loopv(sounds) removesound(i);
    mapsounds.clear(); // could introduce problems
}

void getsounds(bool mapsnd, int idx, int prop)
{
    std::map<const int, soundslot>& soundset = mapsnd ? mapsounds : gamesounds;
    if(idx < 0) intret(soundset.size());
    else if(soundset.find(idx) != soundset.end())
    {
        if(prop < 0) intret(4);
        else switch(prop)
        {
            case 0: intret(soundset[idx].vol); break;
            case 1: intret(soundset[idx].maxrad); break;
            case 2: intret(soundset[idx].minrad); break;
            case 3: result(soundset[idx].name.c_str()); break;
            default: break;
        }
    }
}
ICOMMAND(0, getsound, "ibb", (int *n, int *v, int *p), getsounds(*n!=0, *v, *p));

void getcursounds(int idx, int prop)
{
    if(idx < 0) intret(sounds.length());
    else if(sounds.inrange(idx))
    {
        if(prop < 0) intret(19);
        else switch(prop)
        {
            case 0: intret(sounds[idx].vol); break;
            case 1: intret(sounds[idx].curvol); break;
            case 2: intret(sounds[idx].curpan); break;
            case 3: intret(sounds[idx].flags); break;
            case 4: intret(sounds[idx].maxrad); break;
            case 5: intret(sounds[idx].minrad); break;
            case 6: intret(sounds[idx].material); break;
            case 7: intret(sounds[idx].millis); break;
            case 8: intret(sounds[idx].ends); break;
            case 9: intret(sounds[idx].slotnum); break;
            case 10: intret(sounds[idx].chan); break;
            case 11: defformatstring(pos, "%.f %.f %.f", sounds[idx].pos.x, sounds[idx].pos.y, sounds[idx].pos.z); result(pos); break;
            case 12: defformatstring(oldpos, "%.f %.f %.f", sounds[idx].oldpos.x, sounds[idx].oldpos.y, sounds[idx].oldpos.z); result(oldpos); break;
            case 13: intret(sounds[idx].valid() ? 1 : 0); break;
            case 14: intret(sounds[idx].playing() ? 1 : 0); break;
            case 15: intret(sounds[idx].flags&SND_MAP ? 1 : 0); break;
            case 16: intret(sounds[idx].owner!=nullptr ? 1 : 0); break;
            case 17: intret(sounds[idx].owner==camera1 ? 1 : 0); break;
            case 18: intret(client::getcn(sounds[idx].owner)); break;
            default: break;
        }
    }
}
ICOMMAND(0, getcursound, "bb", (int *n, int *p), getcursounds(*n, *p));

Mix_Music *loadmusic(std::string name)
{
    if(!musicstream) musicstream = openzipfile(name.c_str(), "rb");
    if(musicstream)
    {
        if (musicrw == nullptr) musicrw = musicstream->rwops();
        // if musicrw is still a nullptr, delete the musicstream
        if (musicrw == nullptr) DELETEP(musicstream);
    }
    if(musicrw != nullptr) music = Mix_LoadMUSType_RW(musicrw, MUS_NONE, 0);
    else music = Mix_LoadMUS(findfile(name.c_str(), "rb"));
    if(!music)
    {
        if(musicrw != nullptr) { SDL_FreeRW(musicrw); musicrw = nullptr; }
        DELETEP(musicstream);
    }
    return music;
}

bool playmusic(std::string name, std::string cmd)
{
    if (nosound) {
        return false;
    }

    // stop music with cmd
    stopmusic(false);


    if (!name.empty())
    {
        std::string relative_path;
        std::string dirs[] = { "", "sounds/" };
        std::string exts[] = { "", ".wav", ".ogg" };
        for (auto dir : dirs)
        {
            for (auto ext : exts)
            {
                relative_path = dir + name + ext;
                if (loadmusic(relative_path) != nullptr)
                {
                    musicfile = name;

                    if (!cmd.empty()) {
                        musicdonecmd = cmd;
                    } else {
                        // clear the previous musicdonecmd
                        musicdonecmd = "";
                    }
                    musicdonetime = -1;

                    if (musicfadein) {
                        Mix_FadeInMusic(music, cmd.empty() ? -1 : 0, musicfadein);
                    } else {
                        Mix_PlayMusic(music, cmd.empty() ? -1 : 0);
                    }

                    Mix_VolumeMusic(int((mastervol / 255.f) * (musicvol / 255.f) * MIX_MAX_VOLUME));
                    changedvol = true;
                    return true;
                }
            }
        }

        // if the function didn't return true earlier, we know that the music couldn't be played
        conoutf("\fCcould not play music: %s", name.c_str());
    }
    return false;
}

COMMANDN(0, music, playmusic, "ss");

bool playingmusic(bool check)
{
    if(music)
    {
        if(Mix_PlayingMusic())
        {
            if(musicdonetime >= 0) musicdonetime = -1;
            return true;
        }
        if(check)
        {
            if(musicdonetime < 0)
            {
                musicdonetime = totalmillis;
                return true;
            }
            if(totalmillis-musicdonetime < 500) return true;
        }
    }
    return false;
}

void smartmusic(bool cond, bool init)
{
    if(init) canmusic = true;
    if(!canmusic || nosound || !mastervol || !musicvol || (!cond && Mix_PlayingMusic()) || !*titlemusic) return;
    if(!playingmusic() || (cond && musicfile == titlemusic)) playmusic(titlemusic);
    else
    {
        Mix_VolumeMusic(int((mastervol/255.f)*(musicvol/255.f)*MIX_MAX_VOLUME));
        changedvol = true;
    }
}
ICOMMAND(0, smartmusic, "i", (int *a), smartmusic(*a));

static Mix_Chunk *loadwav(const char *name)
{
    Mix_Chunk *c = nullptr;
    stream *z = openzipfile(name, "rb");
    if(z)
    {
        SDL_RWops *rw = z->rwops();
        if(rw)
        {
            c = Mix_LoadWAV_RW(rw, 0);
            SDL_FreeRW(rw);
        }
        delete z;
    }
    if(!c) c = Mix_LoadWAV(findfile(name, "rb"));
    return c;
}

int registersound(std::string name, int volume, int max_radius, int min_radius, int value, int index, bool is_map_sound)
{
    if (index == -1) {
        // don't register game sounds when no index is specified, as game sounds are not dynamic but fixed
        if (!is_map_sound) {
            return -1;
        }
        else
        {
            // map sounds will be registered in order, since they're dynamically loaded
            index = int(mapsounds.size());
        }
    }

    std::map<const int, soundslot>& soundset = is_map_sound ? mapsounds : gamesounds;

    if (volume <= 0 || volume >= 255) { volume = 255;    }
    if (max_radius <= 0) {              max_radius = -1; }
    min_radius = std::max(-1, min_radius);

    if (value == 1)
    {
        // if the sound has already been loaded, return its index
        for (size_t i = 0; i < soundset.size(); i++)
        {
            soundslot& slot = soundset[i];
            if (   slot.vol == volume
                && slot.maxrad == max_radius
                && slot.minrad == min_radius
                && slot.name == name.c_str())
            {
                return i;
            }
        }
    }

    // the sound has not been added yet, let's create a new instance
    soundset[index]        = soundslot();
    soundset[index].name   = newstring(name.c_str());
    soundset[index].vol    = volume;
    soundset[index].maxrad = max_radius;
    soundset[index].minrad = min_radius;

    // just used for weapons that don't have certain sound effects, e.g. some weapons don't have a zoom sound
    // so we don't create an offset inside the vector, we have to fill those places with some
    // placeholders
    if (name == "<none>")
    {
        printf("Do not load %d\n", index);
        return soundset.size() - 1;
    }

    // load the actual samples here
    std::string sample_name;
    soundsample* sample = nullptr;
    int i = 1;
    int loaded_samples = 0;

    // naming scheme is: shell.ogg, shell2.ogg, shell3.ogg, shell4.ogg
    // the values would be name: "shell" and value: 4
    // so the first element has to always be loaded, that's why do...while
    // after that just go on counting up 'til we reach the value and load all sounds we find on our way
    do
    {
        // if the value is 1 (the first entry) just use the normal name since there is no such thing as shell1.ogg,
        // but rather shell.ogg and then it continues with shell2.ogg (that's why we start counting from 1)
        if (i == 1) { sample_name = name;
        } else {      sample_name = name + std::to_string(i);  }

        if (!(sample = soundsamples.access(sample_name.c_str())))
        {
            // copy the sample_name because the class soundsample uses char* instead of std::string
            char* s_name = newstring(sample_name.c_str());

            sample = &soundsamples[s_name];
            sample->name = s_name;
            sample->sound = nullptr;
        }

        // if the sample isn't already loaded, load it
        if (!sample->sound)
        {
            std::string relative_path;
            std::string dirs[] = { "", "sounds/" };
            std::string exts[] = { "", ".wav", ".ogg" };
            bool found = false;

            for (auto dir : dirs)
            {
                for (auto ext : exts)
                {
                    relative_path = dir + sample_name + ext;

                    sample->sound = loadwav(relative_path.c_str());
                    // if we can load the sample, set found to true and the exit the loop
                    if (sample->sound != nullptr)
                    {
                        found = true;
                        break;
                    }
                }
                // if we found the entry previously, quit the loop
                if (found) { break; }
            }
        }

        // check if the sample is set now
        if (sample->sound != nullptr)
        {
            soundset[index].samples.add(sample);
            loaded_samples++;
        }

        i++;
    }
    while (i <= value);

    if (loaded_samples < value)
    {
        printf("Failed to load %d sample %s\n", (value - loaded_samples), name.c_str());
    }
    else
    {
        printf("Loaded sound %d: %s\n", index, soundset[index].name.c_str());
    }

    return soundset.size() - 1;
}

void add_game_sound(int index, std::string name, int volume, int max_radius, int min_radius, int value)
{
    registersound(name, volume, max_radius, min_radius, value, index, false);
}

ICOMMAND(0, registersound, "sissi", (char *n, int *v, char *w, char *x, int *u), intret(registersound(n, *v, *w ? parseint(w) : -1, *x ? parseint(x) : -1, *u, -1, false)));
ICOMMAND(0, mapsound, "sissi", (char *n, int *v, char *w, char *x, int *u), intret(registersound(n, *v, *w ? parseint(w) : -1, *x ? parseint(x) : -1, *u, -1, true)));

void calcvol(int flags, int vol, int slotvol, int maxrad, int minrad, const vec &pos, int *curvol, int *curpan, bool liquid)
{
    vec v;
    float dist = pos.dist(camera1->o, v);
    int svol = flags&SND_CLAMPED ? 255 : clamp(vol, 0, 255), span = 127;
    if(!(flags&SND_NOATTEN) && dist > 0)
    {
        if(!(flags&SND_NOPAN) && !soundmono && (v.x != 0 || v.y != 0))
        {
            v.rotate_around_z(-camera1->yaw*RAD);
            span = int(255.9f*(0.5f - 0.5f*v.x/v.magnitude2())); // range is from 0 (left) to 255 (right)
        }
        if(!(flags&SND_NODIST))
        {
            float mrad = int((maxrad > 0 ? maxrad : soundmaxrad)*(flags&SND_MAP ? soundenvscale : soundevtscale)),
                  nrad = minrad > 0 ? (minrad <= mrad ? minrad : mrad) : soundminrad;
            if(dist > nrad)
            {
                if(dist <= mrad) svol = int(svol*(1.f-((dist-nrad)/max(mrad-nrad,1e-16f))));
                else svol = 0;
            }
        }
    }
    if(!(flags&SND_NOQUIET) && svol > 0 && liquid) svol = int(svol*0.65f);
    if(flags&SND_CLAMPED) svol = max(svol, clamp(vol, 0, 255));
    *curvol = clamp(int((mastervol/255.f)*(soundvol/255.f)*(slotvol/255.f)*(svol/255.f)*MIX_MAX_VOLUME*(flags&SND_MAP ? soundenvvol : soundevtvol)), 0, MIX_MAX_VOLUME);
    *curpan = span;
}

void updatesound(int chan)
{
    sound &s = sounds[chan];
    bool waiting = (!(s.flags&SND_NODELAY) && Mix_Paused(chan));
    if((s.flags&SND_NOCULL) || (s.flags&SND_BUFFER) || !soundcull || s.curvol > 0 || s.pos.dist(camera1->o) <= 0)
    {
        if(waiting)
        { // delay the sound based on average physical constants
            bool liquid = isliquid(lookupmaterial(s.pos)&MATF_VOLUME) || isliquid(lookupmaterial(camera1->o)&MATF_VOLUME);
            float dist = camera1->o.dist(s.pos);
            int delay = int((dist/8.f)*(liquid ? 1.5f : 0.35f));
            if(lastmillis >= s.millis+delay)
            {
                Mix_Resume(chan);
                waiting = false;
            }
        }
        if(!waiting)
        {
            Mix_Volume(chan, s.curvol);
            if(!soundmono) Mix_SetPanning(chan, 255-s.curpan, s.curpan);
        }
    }
    else
    {
        removesound(chan);
        if(verbose >= 4) conoutf("culled sound %d (%d)", chan, s.curvol);
    }
}

void updatesounds()
{
    updatemumble();
    if(nosound) return;
    bool liquid = isliquid(lookupmaterial(camera1->o)&MATF_VOLUME);
    loopv(sounds) if(sounds[i].chan >= 0)
    {
        sound &s = sounds[i];
        if((!s.ends || lastmillis < s.ends) && Mix_Playing(sounds[i].chan))
        {
            if(s.owner) s.pos = game::camerapos(s.owner);
            if(s.pos != s.oldpos) s.material = lookupmaterial(s.pos);
            calcvol(s.flags, s.vol, s.slot->vol, s.maxrad, s.minrad, s.pos, &s.curvol, &s.curpan, liquid || isliquid(s.material&MATF_VOLUME));
            s.oldpos = s.pos;
            updatesound(i);
            continue;
        }
        std::map<const int, soundslot>& soundset = s.flags&SND_MAP ? mapsounds : gamesounds;
        while(!s.buffer.empty() && (soundset.find(s.buffer[0]) == soundset.end() || soundset[s.buffer[0]].samples.empty() || !soundset[s.buffer[0]].vol)) s.buffer.remove(0);
        if(!s.buffer.empty())
        {
            int n = s.buffer[0], chan = -1;
            Mix_HaltChannel(i);
            s.buffer.remove(0);
            bool nocull = s.flags&SND_NOCULL || s.pos.dist(camera1->o) <= 0;
            soundslot *slot = &soundset[n];
            soundsample *sample = slot->samples[rnd(slot->samples.length())];
            if((chan = Mix_PlayChannel(i, sample->sound, s.flags&SND_LOOP ? -1 : 0)) < 0)
            {
                int lowest = -1;
                loopv(sounds) if(sounds[i].chan >= 0 && (!(sounds[i].flags&SND_MAP) || s.flags&SND_MAP) && sounds[i].curvol < s.curvol && (lowest < 0 || sounds[i].curvol < sounds[lowest].curvol) && (nocull || (!(sounds[i].flags&SND_NOCULL) && sounds[i].pos.dist(camera1->o) > 0)))
                    lowest = i;
                if(sounds.inrange(lowest))
                {
                    if(verbose >= 4) conoutf("culled channel %d (%d)", lowest, sounds[lowest].curvol);
                    removesound(lowest);
                    chan = Mix_PlayChannel(-1, sample->sound, s.flags&SND_LOOP ? -1 : 0);
                }
            }
            if(chan >= 0)
            {
                while(chan >= sounds.length()) sounds.add();
                sound &t = sounds[chan];
                if(chan == s.chan)
                {
                    i--; // repeat with new sound
                    continue;
                }
                else
                {
                    t.slot = slot;
                    t.vol = s.vol;
                    t.maxrad = s.maxrad;
                    t.minrad = s.minrad;
                    t.material = s.material;
                    t.flags = s.flags;
                    t.millis = s.millis;
                    t.ends = s.ends;
                    t.slotnum = s.slotnum;
                    t.owner = s.owner;
                    t.pos = t.oldpos = s.pos;
                    t.curvol = s.curvol;
                    t.curpan = s.curpan;
                    t.chan = chan;
                    t.hook = s.hook;
                    loopvj(s.buffer) t.buffer.add(s.buffer[j]);
                    updatesound(chan);
                }
            }
        }
        removesound(i);
    }
    if(music || Mix_PlayingMusic())
    {
        if(nosound || !mastervol || !musicvol) stopmusic(false);
        else if(!playingmusic()) musicdone(true);
        else if(changedvol)
        {
            Mix_VolumeMusic(int((mastervol/255.f)*(musicvol/255.f)*MIX_MAX_VOLUME));
            changedvol = false;
        }
    }
}

int playsound(int n, const vec& pos, physent* d, int flags, int vol, int maxrad, int minrad, int* hook, int ends, int* oldhook)
{
    bool flag_buffer  = flags & SND_BUFFER;
    bool flag_map     = flags & SND_MAP;
    bool flag_clamped = flags & SND_CLAMPED;
    bool flag_nocull  = flags & SND_NOCULL;
    bool flag_nodelay = flags & SND_NODELAY;
    bool flag_loop    = flags & SND_LOOP;

    if (  nosound
       || mastervol == 0
       || soundvol == 0
       || ((flag_map || n >= S_GAMESPECIFIC) && client::waiting(true))
       || (d == nullptr && !insideworld(pos)))
    {
        return -1;
    }

    std::map<const int, soundslot>& soundset = flag_map ? mapsounds : gamesounds;

    if (soundset.find(n) != soundset.end())
    {
        if (hook && issound(*hook) && flag_buffer)
        {
            sounds[*hook].buffer.add(n);
            return *hook;
        }

        if (soundset[n].samples.empty() || !soundset[n].vol)
        {
            if(oldhook && issound(*oldhook)) {
                removesound(*oldhook);
            }
            return -1;
        }

        soundslot* slot = &soundset[n];
        if (  !oldhook
           || !issound(*oldhook)
           || (n != sounds[*oldhook].slotnum && slot->name == gamesounds[sounds[*oldhook].slotnum].name))
        {
            oldhook = nullptr;
        }

        vec o = d ? game::camerapos(d) : pos;
        int cvol = 0;
        int cpan = 0;
        int v = clamp(vol >= 0 ? vol : 255, flag_clamped ? 64 : 0, 255);
        int x = maxrad > 0 ? maxrad : (flag_clamped ? getworldsize() : (slot->maxrad > 0 ? slot->maxrad : soundmaxrad));
        int y = minrad >= 0 ? minrad : (flag_clamped ? 32 : (slot->minrad >= 0 ? slot->minrad : soundminrad));
        int mat = lookupmaterial(o);

        bool liquid = isliquid(lookupmaterial(camera1->o) & MATF_VOLUME);
        calcvol(flags, v, slot->vol, x, y, o, &cvol, &cpan, liquid || isliquid(mat & MATF_VOLUME));
        bool nocull = flag_nocull || o.dist(camera1->o) <= 0;

        if (nocull || soundcull == 0 || cvol > 0)
        {
            int chan = -1;
            if (oldhook)
            {
                chan = *oldhook;
            }
            else
            {
                oldhook = nullptr;
                soundsample* sample = slot->samples[rnd(slot->samples.length())];
                chan = Mix_PlayChannel(-1, sample->sound, flag_loop ? -1 : 0);
                if(chan < 0)
                {
                    int lowest = -1;
                    for (int i = 0; i < sounds.length(); i++)
                    {
                        if (   sounds[i].chan >= 0
                            && ( !(sounds[i].flags & SND_MAP) || flag_map )
                            && sounds[i].curvol < cvol
                            && (lowest < 0 || sounds[i].curvol < sounds[lowest].curvol)
                            && (nocull || ( !(sounds[i].flags & SND_NOCULL) && sounds[i].pos.dist(camera1->o) > 0 )))
                        {
                            lowest = i;
                        }
                    }

                    if (sounds.inrange(lowest))
                    {
                        if (verbose >= 4)
                        {
                            conoutf("culled channel %d (%d)", lowest, sounds[lowest].curvol);
                        }

                        removesound(lowest);
                        chan = Mix_PlayChannel(-1, sample->sound, flag_loop ? -1 : 0);
                    }
                }
            }

            if (chan >= 0)
            {
                if (!oldhook && !flag_nodelay) {
                    Mix_Pause(chan);
                }

                while (chan >= sounds.length()) sounds.add();

                sound& s = sounds[chan];
                s.slot = slot;
                s.vol = v;
                s.maxrad = x;
                s.minrad = y;
                s.material = mat;
                s.flags = flags;
                s.millis = oldhook ? sounds[*oldhook].millis : lastmillis;
                s.ends = ends;
                s.slotnum = n;
                s.owner = d;
                s.pos = s.oldpos = o;
                s.curvol = cvol;
                s.curpan = cpan;
                s.chan = chan;

                if (hook)
                {
                    if (issound(*hook) && (!oldhook || *hook != *oldhook)) {
                        removesound(*hook);
                    }
                    *hook = s.chan;
                    s.hook = hook;
                }
                else
                {
                    s.hook = nullptr;
                }

                if (oldhook) {
                    *oldhook = -1;
                }

                updatesound(chan);
                return chan;
            }
            else if (verbose >= 2)
            {
                conoutf("\frcannot play sound %d (%s): %s", n, slot->name.c_str(), Mix_GetError());
            }
        }
        else if (verbose >= 4)
        {
                conoutf("culled sound %d (%d)", n, cvol);
        }
    }
    else if (n > 0 && flag_map)
    {
        // only show this error message if it's a mapsound since we are intentionally
        // not loading sounds for some weapons e.g. S_W_BOUNCE2 or S_W_EXTINGUISH
        conoutf("\frunregistered sound: %d", n);
    }
    if (oldhook && issound(*oldhook))
    {
        removesound(*oldhook);
    }
    return -1;
}

void sound(int *n, int *vol, int *flags)
{
    intret(playsound(*n, camera1->o, camera1, *flags >= 0 ? *flags : SND_FORCED, *vol ? *vol : -1));
}
COMMAND(0, sound, "iib");

void removetrackedsounds(physent *d)
{
    loopv(sounds)
        if(sounds[i].chan >= 0 && sounds[i].owner == d)
            removesound(i);
}

void resetsound()
{
    clearchanges(CHANGE_SOUND);

    if (!nosound) {
        for (int i = 0; i < sounds.length(); i++) {
            removesound(i);
        }

        // call cleanup for all soundsamples
        enumerate(soundsamples, soundsample, sample, sample.cleanup());

        // Stop playing music
        if (music) {
            Mix_HaltMusic();
            Mix_FreeMusic(music);
        }

        if (musicstream) {
            musicstream->seek(0, SEEK_SET);
        }
        Mix_CloseAudio();
        nosound = true;
    }

    initsound();

    if (nosound)
    {
        musicfile = "";
        musicdonecmd = "";
        music = nullptr;
        soundsamples.clear();
        gamesounds.clear();
        mapsounds.clear();
        return;
    }

    rehash(true);

    if (music && loadmusic(musicfile.c_str()))
    {
        if (musicfadein) {
            Mix_FadeInMusic(music, musicdonecmd.empty() ? -1 : 0, musicfadein);
        } else {
            Mix_PlayMusic(music, musicdonecmd.empty() ? -1 : 0);
        }

        Mix_VolumeMusic(int((mastervol / 255.f) * (musicvol / 255.f) * MIX_MAX_VOLUME));
        changedvol = true;
    }
    else
    {
        musicfile = "";
        musicdonecmd = "";
    }
}

COMMAND(0, resetsound, "");

#ifdef WIN32

#include <wchar.h>

#else

#include <unistd.h>

#if _POSIX_SHARED_MEMORY_OBJECTS > 0
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <wchar.h>
#endif

#endif

#if defined(WIN32) || _POSIX_SHARED_MEMORY_OBJECTS > 0
struct MumbleInfo
{
    int version, timestamp;
    vec pos, front, top;
    wchar_t name[256];
};
#endif

#ifdef WIN32
static HANDLE mumblelink = nullptr;
static MumbleInfo *mumbleinfo = nullptr;
#define VALID_MUMBLELINK (mumblelink && mumbleinfo)
#elif _POSIX_SHARED_MEMORY_OBJECTS > 0
static int mumblelink = -1;
static MumbleInfo *mumbleinfo = (MumbleInfo *)-1;
#define VALID_MUMBLELINK (mumblelink >= 0 && mumbleinfo != (MumbleInfo *)-1)
#endif

#ifdef VALID_MUMBLELINK
VARF(IDF_PERSIST, mumble, 0, 1, 1, { if(mumble) initmumble(); else closemumble(); });
#else
VARF(IDF_PERSIST, mumble, 0, 0, 1, { if(mumble) initmumble(); else closemumble(); });
#endif

void initmumble()
{
    if(!mumble) return;
#ifdef VALID_MUMBLELINK
    if(VALID_MUMBLELINK) return;

    #ifdef WIN32
        mumblelink = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, "MumbleLink");
        if(mumblelink)
        {
            mumbleinfo = (MumbleInfo *)MapViewOfFile(mumblelink, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(MumbleInfo));
            if(mumbleinfo) wcsncpy(mumbleinfo->name, (const wchar_t *)versionuname, 256);
        }
    #elif _POSIX_SHARED_MEMORY_OBJECTS > 0
        defformatstring(shmname, "/MumbleLink.%d", getuid());
        mumblelink = shm_open(shmname, O_RDWR, 0);
        if(mumblelink >= 0)
        {
            mumbleinfo = (MumbleInfo *)mmap(nullptr, sizeof(MumbleInfo), PROT_READ|PROT_WRITE, MAP_SHARED, mumblelink, 0);
            if(mumbleinfo != (MumbleInfo *)-1) wcsncpy(mumbleinfo->name, (const wchar_t *)versionuname, 256);
        }
    #endif
    if(!VALID_MUMBLELINK) closemumble();
#else
    conoutft(CON_MESG, "Mumble positional audio is not available on this platform.");
#endif
}

void closemumble()
{
#ifdef WIN32
    if(mumbleinfo) { UnmapViewOfFile(mumbleinfo); mumbleinfo = nullptr; }
    if(mumblelink) { CloseHandle(mumblelink); mumblelink = nullptr; }
#elif _POSIX_SHARED_MEMORY_OBJECTS > 0
    if(mumbleinfo != (MumbleInfo *)-1) { munmap(mumbleinfo, sizeof(MumbleInfo)); mumbleinfo = (MumbleInfo *)-1; }
    if(mumblelink >= 0) { close(mumblelink); mumblelink = -1; }
#endif
}

static inline vec mumblevec(const vec &v, bool pos = false)
{
    // change from X left, Z up, Y forward to X right, Y up, Z forward
    // 8 cube units = 1 meter
    vec m(-v.x, v.z, v.y);
    if(pos) m.div(8);
    return m;
}

void updatemumble()
{
#ifdef VALID_MUMBLELINK
    if(!VALID_MUMBLELINK) return;

    static int timestamp = 0;

    mumbleinfo->version = 1;
    mumbleinfo->timestamp = ++timestamp;

    mumbleinfo->pos = mumblevec(camera1->o, true);
    mumbleinfo->front = mumblevec(vec(RAD*camera1->yaw, RAD*camera1->pitch));
    mumbleinfo->top = mumblevec(vec(RAD*camera1->yaw, RAD*(camera1->pitch+90)));
#endif
}

