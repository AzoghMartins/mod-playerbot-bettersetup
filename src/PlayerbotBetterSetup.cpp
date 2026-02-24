/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 */

#include "Chat.h"
#include "Config.h"
#include "Player.h"
#include "ScriptMgr.h"

class PlayerbotBetterSetupPlayerScript : public PlayerScript
{
public:
    PlayerbotBetterSetupPlayerScript() : PlayerScript("PlayerbotBetterSetupPlayerScript") { }

    void OnPlayerLogin(Player* player) override
    {
        if (!sConfigMgr->GetOption<bool>("PlayerbotBetterSetup.Enable", true))
            return;

        ChatHandler(player->GetSession()).SendSysMessage("mod-playerbot-bettersetup is enabled.");
    }
};

void AddPlayerbotBetterSetupScripts()
{
    new PlayerbotBetterSetupPlayerScript();
}
