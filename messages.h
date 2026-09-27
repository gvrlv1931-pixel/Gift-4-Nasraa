#ifndef MESSAGES_H
#define MESSAGES_H

// ============================================================================
//  MESSAGES for the Study Buddy
//  Change the name here and it updates everywhere.
//  Plain keyboard characters only: the screen font cannot draw emoji,
//  curly quotes or long dashes. Add or remove lines freely.
//  Lines in the START banks must contain exactly one %u (the minutes).
// ============================================================================

#define BUDDY_NAME "Nasraa"

// ---- Right button: pep talks (any time outside a focus session) ------------
const char* const MSG_PEP[] = {
  "You're smarter than your to-do list thinks, " BUDDY_NAME ".",
  "One page at a time. I'll hold the vibes.",
  "Your future self just sent a thank-you note.",
  "Tiny progress is still progress. I checked.",
  BUDDY_NAME " vs. the deadline. My money's on " BUDDY_NAME ".",
  "You don't have to feel ready. Just start small.",
  "Proud of you for showing up today, " BUDDY_NAME ".",
  "Hard means you're learning. Not failing. Learning.",
  "Messy first draft? Perfect. That's the recipe.",
  "I believe in you. I have no hands, so you type.",
  "Deep breath. Shoulders down. You're doing fine.",
  "The master's is lucky to have you, " BUDDY_NAME ".",
  "Rest is part of the work. Put that in your notes.",
  "Five minutes of work beats an hour of worrying.",
  "I'd give you a gold star but I'm a stick.",
  "Academia is tough. You're tougher.",
  "Stay cute, stay hydrated, be kind to yourself.",
  "I am a delicate academic instrument and I say: you've got this.",
  "Somewhere, a reference list is scared of you.",
  "You've done hard things before. This is one more.",
  "Your brain is doing its best. Be nice to it.",
  "When did you last text a friend? Go on, I'll wait.",
  "Have you eaten today, " BUDDY_NAME "? Real food counts.",
  "Academia is weird and hard. You're allowed to find it hard."
};

// ---- Right button during a focus session: stay-on-task peer pressure -------
const char* const MSG_FOCUS_PEP[] = {
  "Eyes on the page, " BUDDY_NAME ". I'm watching. Lovingly.",
  "Phone down. Brain on. You've got this.",
  "Just finish this paragraph. Then the next one.",
  "I'm focusing too. Look at my serious face.",
  "The timer does the counting. You do the thinking.",
  "No tab-hopping! I can sense it.",
  "You're in the zone. Don't let the zone escape.",
  "Future " BUDDY_NAME " will be so smug about this.",
  "Keep going. I'm right here.",
  "One more sentence. Then another. That's the whole trick.",
  "Stuck? Write the ugly version first.",
  "Focus mode: on. Snacks: later."
};

// ---- Break time: self-care side quests -------------------------------------
const char* const MSG_SELF_CARE[] = {
  "Stand up and stretch like a cat.",
  "Water check! Go have a glass.",
  "Look at something far away for 20 seconds.",
  "Snack time? Brains run on snacks.",
  "Roll your shoulders back five times.",
  "Step away from the screen. I'll guard your notes.",
  "Send someone a silly message. Social battery +1.",
  "Open a window. Fresh air, fresh thoughts.",
  "Dance to one song. Nobody's watching. Except me.",
  "Tidy one tiny thing on your desk.",
  "Shake out your hands. They've been typing like heroes.",
  "Want calm? Press Left twice for guided breathing.",
  "Eyes closed for ten seconds. Pure luxury.",
  "Unclench your jaw. You've been doing the laptop face."
};

// ---- Focus session finished ------------------------------------------------
const char* const MSG_DONE[] = {
  "Session done! You absolute legend.",
  "Look at you go, " BUDDY_NAME "!",
  "Another one in the bag. So proud of you.",
  "Done! Academia fears you.",
  "Nailed it. Your brain deserves a treat.",
  "That's how it's done!",
  "Brilliant work. Tomato collected.",
  "Focus complete. I'm doing a happy dance inside.",
  "You did the thing! I saw it all.",
  "Big brain energy detected."
};

// ---- After being shaken (shown after the scared face) ----------------------
const char* const MSG_SHAKE[] = {
  "Woah! I am a delicate academic instrument, " BUDDY_NAME ".",
  "My tiny brain went all wobbly.",
  "Gentle please! I'm fragile and full of feelings.",
  "Was that an earthquake or was that you?",
  "I'm fine. I'm fine. I'm not fine.",
  "Ahh! I thought we were friends!",
  "My circuits felt that one.",
  "Rude! But I forgive you. Instantly.",
  "Shaking me won't write the essay, " BUDDY_NAME ".",
  "Is this about the deadline? We can talk about it.",
  "Everything is spinning. Is that normal?",
  "Please handle with care. I am very important."
};

// ---- After one sudden hard knock -------------------------------------------
const char* const MSG_JOLT[] = {
  "EEK! You scared me!",
  "What was THAT?!",
  "My heart! Do I have a heart? It's racing anyway.",
  "Warn me next time, " BUDDY_NAME "!",
  "Jumpscare! Not cool.",
  "I nearly dropped my imaginary coffee."
};

// ---- Shaken three times in 30 seconds --------------------------------------
const char* const MSG_SHAKE_AGAIN[] = {
  "Okay. Dizzy AND sulking now. Hmph.",
  "That's three shakes. I'm keeping count.",
  "I need a moment. And a hug. Mostly a moment.",
  "Stop! My eyes are doing loop-the-loops.",
  "Stressed? Try Left twice. Breathing beats shaking."
};

// ---- Picked up / fiddled with during a focus session -----------------------
const char* const MSG_CAUGHT[] = {
  "Caught you! Put me down and get back to it.",
  "Hands off, " BUDDY_NAME "! Eyes on the work.",
  "Nope. We're focusing. I'll be here after.",
  "I see you fidgeting. Back to the page!",
  "Picking me up doesn't count as studying.",
  "Timer's still running, superstar!",
  "Distraction detected. Returning you to work."
};

// ---- Gentle wiggle / cuddle (outside focus) --------------------------------
const char* const MSG_PET[] = {
  "Hehe, that tickles!",
  "Aww, hi to you too!",
  "Hi hi hi! I missed you.",
  "Oh! Cuddle time?",
  "You're my favourite human, " BUDDY_NAME "."
};

// ---- Starting a focus session (one bank per energy level) ------------------
const char* const MSG_START_LOW[] = {
  "Low battery day? %u gentle minutes. Small is fine.",
  "%u minutes. Tiny steps still get you there.",
  "Just %u minutes, " BUDDY_NAME ". I'll sit with you.",
  "%u easy minutes. Start with the smallest bit."
};
const char* const MSG_START_OKAY[] = {
  "%u minutes. You and me, " BUDDY_NAME ". Let's go!",
  "%u minute focus. I'll keep watch.",
  "Timer's on for %u minutes. Phone away!",
  "%u focused minutes. You've got this."
};
const char* const MSG_START_FULL[] = {
  "Full battery! %u minutes of greatness incoming.",
  "%u minutes. Let's show that essay who's boss.",
  "Power mode: %u minutes. I'm so ready.",
  "%u minutes, " BUDDY_NAME ". Unstoppable mode."
};

// ---- Halfway through a focus session ---------------------------------------
const char* const MSG_HALF[] = {
  "Halfway there, " BUDDY_NAME "! Keep rolling.",
  "Halfway! You're doing brilliantly.",
  "Half done. The other half is nervous.",
  "Halfway point. Sip of water, then onwards."
};

// ---- Final minute of a focus session ---------------------------------------
const char* const MSG_LAST_MIN[] = {
  "One minute left. Finish that thought!",
  "Final minute! Sprint time.",
  "60 seconds! Wrap up the sentence."
};

// ---- Break finished --------------------------------------------------------
const char* const MSG_BREAK_OVER[] = {
  "Break's over! Ready for another round?",
  "Refreshed? Let's go again, " BUDDY_NAME ".",
  "Back to it? I'm ready when you are.",
  "Break done. Your notes miss you."
};

// ---- Woken up from a nap ---------------------------------------------------
const char* const MSG_WAKE[] = {
  "Oh! Hi " BUDDY_NAME "! I was just resting my eyes.",
  "Hello! Did you miss me?",
  "I'm up! I'm up!",
  "Yawn... okay, study time?",
  "You're back! Best part of my day."
};

// ---- Finished a breathing exercise -----------------------------------------
const char* const MSG_BREATH_DONE[] = {
  "Lovely breathing. Calmer brain, happier " BUDDY_NAME ".",
  "That was nice. Ready to take on the world?",
  "Calm mode unlocked. Well done."
};

// ---- Session stopped early -------------------------------------------------
const char* const MSG_STOPPED[] = {
  "Stopped. Rest is part of the work too.",
  "All good. We'll go again later.",
  "Timer off. Be kind to yourself, " BUDDY_NAME "."
};

// ---- Paused (or waiting after a break) for a long time ---------------------
const char* const MSG_STILL_THERE[] = {
  "Still there, " BUDDY_NAME "? The timer's waiting.",
  "Press M5 when you're back. No rush.",
  "Hello? I'm keeping your seat warm."
};

// ---- Power on greeting -----------------------------------------------------
const char* const MSG_HELLO[] = {
  "Hi " BUDDY_NAME "! Ready when you are.",
  "Good to see you, " BUDDY_NAME "!",
  "Hello! Shall we do some work?",
  "Hi! Press M5 when you want to focus."
};

// ---- First-ever start: the welcome tour ------------------------------------
const char* const MSG_WELCOME[] = {
  "Hi " BUDDY_NAME "! I'm your new study buddy.",
  "Press M5 (the big button) for a focus timer. I'll keep you company.",
  "Right button: pep talks whenever you need a boost.",
  "Left button: your streak. Press again for a breathing break.",
  "Please don't shake me. I scare easily!",
  "Let's do great things together, " BUDDY_NAME "."
};

// ---- Level titles (a new one every 50 XP; a session is worth 10 XP) --------
const char* const LEVEL_TITLES[] = {
  "Fresh Sprout", "Page Turner", "Note Ninja", "Coffee Scholar",
  "Library Legend", "Citation Queen", "Thesis Wizard", "Academic Icon"
};

#define MSG_LOW_BATTERY "I'm getting sleepy... charge me soon, " BUDDY_NAME "?"

#endif
