#ifndef STARTUP_MESSAGE_H_
#define STARTUP_MESSAGE_H_

static const char* const welcome[] = {"Welcome to se!", "Good day, Human", "Happy Coding", "se: the Simple Editor",
    "Time to get some progress done!", "Ready for combat", "Initialising...Done",  "loaded in %%d seconds",
    "Fun fact: vscode has over two times as many lines describing dependencies than se has in total",
    "You look based", "Another day, another bug to fix", "Who needs a mouse ¯\\_(ツ)_/¯", "grrgrrggghhaaaaaa (╯°□ °）╯︵ ┻━┻",
    "┬┴┬┤(･_├┬┴┬┴┬┴┬┤ʖ ͡°) ├┬┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴┬┴", "ʰᵉˡˡᵒ"};

static void
choose_random_message()
{
    writef_to_status_bar(welcome[rand() % LEN(welcome)]);
}

static const struct extension startup_message = {
    .enable = choose_random_message
};


#endif // STARTUP_MESSAGE_H_
