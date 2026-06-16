# Roadmap

- [x] Render the ckeckbox too, the "- [ ]" must turn into a clickable checkbox
- [x] The popup that let me interact with the filesystem Must also include the delete functionality (If I delete a folder all its content is deleted too). Moreover I think there is a small bug because the popup window is too small an I can't see the whole "New Note" and "New Folder"
- [x] The folder must be folded and unfolded with just 1 click and it must be easily recognizable as a folder with a folder icon jest before the name of the folder
- [x] Add a quick file picker, it must be accessible through ctrl + P and work like the search but only on the titles of the notes
- [x] I would like to be able to scroll the page more than its actual length, make it like it can scroll until the last line is on thop of the screen
- [x] Update the automatic list creation (bullet point, numeric, checkbox) to create a new entry only if i'm at the end of the line
- [x] I want you to better render the code blocks hiding the three ` and making a proper rectangle over the n lines the block extends (for code blocks in general give the user the possibiliti to copy the whole thing by clicking a small icon on the top right corner of the code block)
- [x] Give the markdown quote the same behavior of the lists, When hitting enter at the end of the line a new quote is automatically created
- [x] Give horizontal lines a full with coverage of the page when rendered
- [ ] Is it possible to better render the tables like in a real markdown file?
- [x] Collapse side panel if resized below the minimum size (keep it resizable if the mouse goes to the screen's left border)
- [x] Make the main content window size adjustable in the settings
- [x] Make the side panel elements bigger
- [x] Give the notes and folder a better graphical separation such that it is clear just by looking where the action for a specific note/folder starts and end
- [x] Add a vertical line to the side of the notes inside a folder to make it clearer that they are sub-notes
- [x] Add a note in the gear icon: "Manual" where we show the user a note with all the Emerald functionalities
- [x] I want Emerald to open the last edited note when it opens (If no previous note it just behave like now) and add a setting to set a specific note as the default "Home" of the Vault
- [x] Add a Setting to choose the location for new notes creation. The default is the Vault root but it can be changed with any folder inside the Vault folder
- [x] Help me gather ideas on how to give the arrows a shortcut to quickly go forward and backwards
- [x] Is it possible to add collapsable paragraph? they must be collapsable clicking an arrow to the left of the title and the collapse logic is: collapse untile another title of the same level is found
- [ ] (deferred) Is it possible to add images to the main view?
- [x] There is a thin black line at the bottom of the screen what is its purpose? is it possible to remove it?

==New features:==
- [ ] When hovering a Note or a folder in the side panel the highlight create a separate highlight zone to the left of the note/folder, can you remove it?
- [ ] Add the icon to the application. I've created a new folder named icons in the project root, set EmeraldClean as the app icon
- [ ] Add search in the file, move Global search to ctrl + shift + F and the file search to ctrl + F
- [ ] Make the scroll bar thinner
- [ ] I would like to be able to drag the files and folders in the side panel to move them in and out the folder structure
- [ ] Give the code block a thin header of a complementary color. On the left there is the language the block contains (specified right after the first ``` and Text as default) and to the right the copy icon (that must copy the content if clicked)
- [ ] The thin black line at the bottom is still there can you remove it?
- [ ] If i resize the side panel when i'm at the end of the file Something strange happen to the content adaptation of the file: The view scrolls down a lot and to go back to the content of the file i have to do multiple scrolls up
- [ ] Can you check if there is a bug when I add a default Note or default creation note folder and then remove it?
- [ ] If I delete a note and there is a default Note display that, otherwise open the last opened one, otherwise open the untitled note.
- [ ] If i click on the resize bar it automatically collapse and if i click the collapsed bar it automatically reopen at the menimum width
- [ ] Question: is it possible to make Emerald real time aware of the file changes made by another program? Right now it seems like it just ignore the changes and then overwrite the file with the one it has in the buffer.
- [ ] (deferred) Add a setting to enable the creation of a "Mascotte" of the file. It will use an extensible set of image on wich the color will be modified following an alogorithm i still have to think. The idea behind this point is to have something visual to associate to the note and that i can use in mnemonic techniques like the memory palace. Leave this point as the last one to implement and give me your opinions on the implementation.


See also [[Welcome]].
