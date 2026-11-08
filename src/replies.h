#ifndef GX_REPLIES_H
#define GX_REPLIES_H
// Exact normalized phrases only. These replies never generate text or run actions.
struct FixedReply {const char* phrases;const char* answer;};
static const FixedReply gx_replies[]={
 {"hello|hi|hey|hey there|hello there|hiya|howdy|yo|sup","Hello, {name}. Good to see you. What are we doing today?"},
 {"good morning|morning","Good morning, {name}. Ready when you are."},
 {"good afternoon|afternoon","Good afternoon, {name}. What can I help with?"},
 {"good evening|evening","Good evening, {name}. Time for a game or a little organizing?"},
 {"good night|goodnight|night","Good night. Your saved notes will be here next time. Session timers stop when you close 360AI."},
 {"bye|goodbye|see you|see you later|later|catch you later","See you later, {name}. Have a good one."},
 {"thanks|thank you|thank you very much|thanks a lot|cheers|thx|ty","You're welcome."},
 {"how are you|how are you doing|how is it going|hows it going","Ready to help. My supported commands work offline without loading a model."},
 {"whats up|what is up|whats new","Ready for whatever you have planned. Try a calculation, a timer or a saved note."},
 {"nice|cool|awesome|great|sounds good|thats cool|thats great","Glad that works for you."},
 {"good job|well done|nice work|you are helpful|youre helpful","Thanks. What would you like to do next?"},
 {"sorry|my bad|oops","No problem. We can start again, or use undo for the last saved-data change."},
 {"i am bored|im bored|what should i do","Try a short game session, organize your notes, or ask for a joke. You can also teach me an answer about your favorite game."},
 {"i am tired|im tired","A break sounds useful. I can set a short timer while 360AI stays open."},
 {"i had a bad day|bad day|im having a bad day","That sounds rough. Want a distraction, a joke, or somewhere to write a note about it?"},
 {"i am happy|im happy|i had a good day","Good to hear. Anything worth remembering? You can save it as a note."},
 {"do you like xbox 360|what do you think about xbox 360|what do you think of xbox 360|what do you think about the xbox 360|what do you think of the xbox 360","The Xbox 360 has a great game library and a lasting homebrew community. Running this little assistant on it is a fun way to give the console another job."},
 {"what is your favorite game|whats your favorite game","I don't play games, but Halo 3 is a fitting conversation starter on an Xbox 360. Teach favorite game: your choice saves your own answer."},
 {"do you play games|can you play games","I can talk about supported topics, keep notes and run tools. I don't control or play your games."},
 {"who are you|what are you|what is your name|whats your name","I'm 360 Assistant, built into 360AI. I use predefined replies and local commands. Type help to see what I can do."},
 {"are you online|are you connected to the internet|do you need internet|does this work offline","This assistant works offline. Its replies, notes, timers and tools run here on your Xbox."},
 {"are you real|are you alive|are you human","I'm software running on your Xbox. My answers come from predefined replies, saved answers and supported tools."},
 {"can you learn|can i teach you|how do i teach you","Use teach favorite game: Halo 3. I save that phrase and answer locally. List learned answers shows them; forget answer favorite game removes one."},
 {"can you remember things|do you have memory","I can save notes, your name and taught answers on your drive. Session timers and conversation follow-ups reset when the app closes."},
 {"can you hear me|can you speak|talk to me out loud","Use the controller keyboard to talk to me. Microphone input and spoken output are not available."},
 {"can you help|can you help me|i need help","Of course. Open Quick tools or type help. I can calculate, convert units, save notes and manage timers."},
 {"what can i ask|what should i ask|give me an example","Try calculate 8 * 7, convert 5 miles to km, set timer 5 minutes called tea, or remember to charge the controller."},
 {"tell me something|tell me something interesting","This assistant answers without loading any model weights. You can keep your Xbox useful for notes, timers and calculations even with no GGUF files installed."},
 {"i dont understand|i do not understand|what do you mean","Tell me which part was unclear, or open Quick tools to use the controller menus."},
 {"can you browse the web|can you search the internet|can you google something","I have no live web lookup in this mode. I can answer supported local questions and search your saved notes."}
};
#endif
