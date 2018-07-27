# Merge log

Scroll down for the original README.md!

Base revision: 5a4ed10982da69f0fdb594617fdcb38735aa3537

|Pull Request|Commit|Title|Author|Merged?|
|----|----|----|----|----|
|[6](https://github.com/citra-emu/citra-canary/pull/6)|[a53a8d3](https://github.com/citra-emu/citra-canary/pull/6/files/)|Canary Base (MinGW Test)|[liushuyu](https://github.com/liushuyu)|Yes|
|[4000](https://github.com/citra-emu/citra/pull/4000)|[8aa55b7](https://github.com/citra-emu/citra/pull/4000/files/)|dist/languages: Update translations|[Hexagon12](https://github.com/Hexagon12)|Yes|
|[3998](https://github.com/citra-emu/citra/pull/3998)|[b4710da](https://github.com/citra-emu/citra/pull/3998/files/)|service/boss: Migrate to ServiceFramework|[NarcolepticK](https://github.com/NarcolepticK)|Yes|
|[3992](https://github.com/citra-emu/citra/pull/3992)|[cce882b](https://github.com/citra-emu/citra/pull/3992/files/)|Services/HLE: Implement PrepareToCloseLibraryApplet and CloseLibraryApplet|[Subv](https://github.com/Subv)|Yes|
|[3988](https://github.com/citra-emu/citra/pull/3988)|[921037a](https://github.com/citra-emu/citra/pull/3988/files/)|citra_qt/multiplayer: allow blocking other players in chat room|[zhaowenlan1779](https://github.com/zhaowenlan1779)|Yes|
|[3986](https://github.com/citra-emu/citra/pull/3986)|[79a38f8](https://github.com/citra-emu/citra/pull/3986/files/)|citra_qt/configuration: fix input configuration disappearing after changing languages|[zhaowenlan1779](https://github.com/zhaowenlan1779)|Yes|
|[3977](https://github.com/citra-emu/citra/pull/3977)|[8a2c44b](https://github.com/citra-emu/citra/pull/3977/files/)|Add virtual bad word list; Load if dump is missing|[B3n30](https://github.com/B3n30)|Yes|
|[3951](https://github.com/citra-emu/citra/pull/3951)|[e61b7d2](https://github.com/citra-emu/citra/pull/3951/files/)|service/cfg, citra_qt: add country code configuration|[zhaowenlan1779](https://github.com/zhaowenlan1779)|Yes|
|[3924](https://github.com/citra-emu/citra/pull/3924)|[1d7dc5a](https://github.com/citra-emu/citra/pull/3924/files/)|citra_qt: Settings (configuration) default value fix|[zhaowenlan1779](https://github.com/zhaowenlan1779)|Yes|
|[3917](https://github.com/citra-emu/citra/pull/3917)|[56488a9](https://github.com/citra-emu/citra/pull/3917/files/)|am: Fix DLC loading.|[Steveice10](https://github.com/Steveice10)|Yes|
|[3910](https://github.com/citra-emu/citra/pull/3910)|[a85447d](https://github.com/citra-emu/citra/pull/3910/files/)|gl_rasterizer: implement mipmap by forwarding PICA mipmap configuration|[wwylele](https://github.com/wwylele)|Yes|


End of merge log. You can find the original README.md below the break.

------

**BEFORE FILING AN ISSUE, READ THE RELEVANT SECTION IN THE [CONTRIBUTING](https://github.com/citra-emu/citra/blob/master/CONTRIBUTING.md#reporting-issues) FILE!!!**

Citra
==============
[![Travis CI Build Status](https://travis-ci.org/citra-emu/citra.svg?branch=master)](https://travis-ci.org/citra-emu/citra)
[![AppVeyor CI Build Status](https://ci.appveyor.com/api/projects/status/sdf1o4kh3g1e68m9?svg=true)](https://ci.appveyor.com/project/bunnei/citra)

Citra is an experimental open-source Nintendo 3DS emulator/debugger written in C++. It is written with portability in mind, with builds actively maintained for Windows, Linux and macOS.

Citra emulates a subset of 3DS hardware and therefore is useful for running/debugging homebrew applications, and it is also able to run many commercial games! Some of these do not run at a playable state, but we are working every day to advance the project forward. (Playable here means compatibility of at least "Okay" on our [game compatibility list](https://citra-emu.org/game).)

Citra is licensed under the GPLv2 (or any later version). Refer to the license.txt file included. Please read the [FAQ](https://citra-emu.org/wiki/faq/) before getting started with the project.

Check out our [website](https://citra-emu.org/)!

For development discussion, please join us at #citra-dev on freenode.

### Development

Most of the development happens on GitHub. It's also where [our central repository](https://github.com/citra-emu/citra) is hosted.

If you want to contribute please take a look at the [Contributor's Guide](CONTRIBUTING.md) and [Developer Information](https://github.com/citra-emu/citra/wiki/Developer-Information). You should as well contact any of the developers in the forum in order to know about the current state of the emulator because the [TODO list](https://docs.google.com/document/d/1SWIop0uBI9IW8VGg97TAtoT_CHNoP42FzYmvG1F4QDA) isn't maintained anymore.

If you want to contribute to the user interface translation, please checkout [citra project on transifex](https://www.transifex.com/citra/citra). We centralize the translation work there, and periodically upstream translation.

### Building

* __Windows__: [Windows Build](https://github.com/citra-emu/citra/wiki/Building-For-Windows)
* __Linux__: [Linux Build](https://github.com/citra-emu/citra/wiki/Building-For-Linux)
* __macOS__: [macOS Build](https://github.com/citra-emu/citra/wiki/Building-for-macOS)


### Support
We happily accept monetary donations or donated games and hardware. Please see our [donations page](https://citra-emu.org/donate/) for more information on how you can contribute to Citra. Any donations received will go towards things like:
* 3DS consoles for developers to explore the hardware
* 3DS games for testing
* Any equipment required for homebrew
* Infrastructure setup
* Eventually 3D displays to get proper 3D output working

We also more than gladly accept used 3DS consoles, preferably ones with firmware 4.5 or lower! If you would like to give yours away, don't hesitate to join our IRC channel #citra on [Freenode](http://webchat.freenode.net/?channels=citra) and talk to neobrain or bunnei. Mind you, IRC is slow-paced, so it might be a while until people reply. If you're in a hurry you can just leave contact details in the channel or via private message and we'll get back to you.
