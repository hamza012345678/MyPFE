import google.generativeai as genai
import json
import os

# ========= CONFIGURATION ==========
API_KEYS_FILE = "api_keys.json"
MODEL_NAME = "gemini-1.5-flash-latest" # <<< RECOMMENDED VALID MODEL NAME
CONTEXT_FILE = "global_chat_history.json"
INITIAL_PROMPT = """
You are a helpful assistant specialized in software development and analyzes.
"""

# ========= UTILITY FUNCTIONS ==========
def load_api_keys(filepath=API_KEYS_FILE):
    """Loads API keys from a JSON file."""
    if os.path.exists(filepath):
        with open(filepath, "r", encoding="utf-8") as f: # Added encoding
            try:
                keys_data = json.load(f)
                if isinstance(keys_data, list) and all(isinstance(key, str) for key in keys_data):
                    return keys_data
                elif isinstance(keys_data, dict) and "keys" in keys_data and isinstance(keys_data["keys"], list):
                     return keys_data["keys"]
                else:
                    print(f"⚠️ Warning: {filepath} has an unexpected format. Expected a list of keys or a dict with a 'keys' list.")
                    return []
            except json.JSONDecodeError:
                print(f"⚠️ Warning: Could not decode JSON from {filepath}.")
                return []
    else:
        print(f"ℹ️ {filepath} not found. Please create it with a list of your API keys, e.g., [\"key1\", \"key2\"]")
        return []

def save_api_keys(keys, filepath=API_KEYS_FILE):
    """Saves API keys to a JSON file."""
    with open(filepath, "w", encoding="utf-8") as f: # Added encoding
        json.dump(keys, f, indent=2)
    print(f"🔑 API keys saved to {filepath}")

def initialize_gemini(api_key):
    """Configures GenAI and returns the model."""
    try:
        genai.configure(api_key=api_key)
        model = genai.GenerativeModel(MODEL_NAME)
        print(f"✅ Gemini configured successfully with API key ...{api_key[-4:]}.") # Added key preview
        return model
    except Exception as e:
        print(f"❌ Error configuring Gemini with API key ...{api_key[-4:]}: {e}") # Added key preview
        print("   Please ensure the API key is valid and has an active billing account if required.")
        return None

# ========= LOAD API KEYS & SELECT INITIAL KEY ==========
api_keys_list = load_api_keys()
current_api_key_index = 0

if not api_keys_list:
    print("❌ No API keys found. Please add your API keys to api_keys.json or directly in the script.")
    print("   api_keys.json should look like: [\"YOUR_FIRST_API_KEY\", \"YOUR_SECOND_API_KEY\"]")
    new_key_input = input("Enter an API key to use for this session (or press Enter to exit): ").strip() # Added strip()
    if new_key_input: # Check if not empty
        api_keys_list.append(new_key_input)
        save_api_keys(api_keys_list)
        # current_api_key_index = 0 (already 0)
    else:
        print("Exiting as no API keys are available.")
        exit()

current_api_key = api_keys_list[current_api_key_index]
model = initialize_gemini(current_api_key)

if not model:
    print("❌ Initial Gemini model initialization failed. Trying to rotate key if possible...")
    success_on_rotate = False
    if len(api_keys_list) > 1:
        for i in range(1, len(api_keys_list)): # Start from the second key
            current_api_key_index = i
            current_api_key = api_keys_list[current_api_key_index]
            model = initialize_gemini(current_api_key)
            if model:
                success_on_rotate = True
                break
    if not success_on_rotate:
        print("❌ Failed to initialize Gemini with any of the provided API keys. Exiting.")
        exit()


# ========= LOAD OR INIT CHAT HISTORY (Context) ==========
# This variable will hold the list of dictionaries loaded from the file.
# This is the format that model.start_chat(history=...) expects.
history_for_model_start = []
if os.path.exists(CONTEXT_FILE):
    try:
        with open(CONTEXT_FILE, "r", encoding="utf-8") as f:
            history_for_model_start = json.load(f)
        print(f"🔁 Loaded global context from {CONTEXT_FILE} ({len(history_for_model_start)} messages).\n")
    except json.JSONDecodeError:
        print(f"⚠️ Could not decode {CONTEXT_FILE}. Starting with a new context.")
        history_for_model_start = [] # Ensure it's an empty list on error
    except Exception as e:
        print(f"⚠️ Error loading {CONTEXT_FILE}: {e}. Starting with a new context.")
        history_for_model_start = [] # Ensure it's an empty list on error
else:
    print(f"ℹ️ Context file {CONTEXT_FILE} not found. Starting with a new context.")

# 'model' should have been successfully initialized by now, or the script would have exited.
chat = model.start_chat(history=history_for_model_start) # Pass the list of dicts

if not history_for_model_start: # If history_for_model_start is still empty (no file or error loading)
    print("✨ Starting new conversation (no context found or loaded).")
    try:
        response = chat.send_message(INITIAL_PROMPT)
        print("🤖 Gemini:\n" + response.text.strip() + "\n")
    except Exception as e:
        print(f"❌ Error sending initial prompt: {e}")
        print("   This might be due to an invalid API key or network issues.")
        if "API_KEY_INVALID" in str(e) or "permission" in str(e).lower() or "quota" in str(e).lower():
             print("   Consider switching API keys using '/switchkey'")


# ========= CHAT LOOP ==========
try:
    while True:
        user_input = input("👤 You: ").strip()

        if not user_input:
            continue

        command_processed = False # Flag to check if input was a command

        if user_input.lower() in ["exit", "quit"]:
            print("💾 Saving conversation and exiting...")
            break

        elif user_input.lower().startswith("/switchkey") or user_input.lower() == "switchkey":
            command_processed = True
            if len(api_keys_list) <= 1:
                print("ℹ️ Only one API key available. Cannot switch.")
            else:
                print("\n--- Switch API Key ---")
                for i, key_preview in enumerate(api_keys_list):
                    preview = key_preview[:4] + "..." + key_preview[-4:]
                    print(f"  {i}: {preview} {'(current)' if i == current_api_key_index else ''}")
                try:
                    new_index_str = input(f"Enter new API key index (0-{len(api_keys_list)-1}) or 'c' to cancel: ").strip().lower() # Added cancel
                    if new_index_str == 'c' or not new_index_str:
                        print("Switch cancelled.")
                    else:
                        new_index = int(new_index_str)
                        if 0 <= new_index < len(api_keys_list):
                            if new_index == current_api_key_index:
                                print("ℹ️ Already using this API key.")
                            else:
                                print(f"🔄 Attempting to switch to API key index {new_index}...")
                                live_history_snapshot = chat.history # Get live history (list of Content objects)
                                
                                current_api_key_index = new_index
                                current_api_key = api_keys_list[current_api_key_index]
                                new_model_instance = initialize_gemini(current_api_key)

                                if new_model_instance:
                                    model = new_model_instance
                                    chat = model.start_chat(history=live_history_snapshot) # Pass list of Content objects
                                    print(f"✅ Switched to API key ending with ...{current_api_key[-4:]}. Context preserved.")
                                else:
                                    print(f"❌ Failed to initialize Gemini with the new key. Consider trying another or checking the key.")
                                    # Optionally revert, but for now, just inform the user.
                                    # A simple revert:
                                    # current_api_key_index = (new_index - 1 + len(api_keys_list)) % len(api_keys_list)
                                    # current_api_key = api_keys_list[current_api_key_index]
                                    # model = initialize_gemini(current_api_key)
                                    # if model: chat = model.start_chat(history=live_history_snapshot)
                        else:
                            print("❌ Invalid index.")
                except ValueError:
                    print("❌ Invalid input. Please enter a number or 'c'.")
                print("----------------------\n")

        elif user_input.lower().startswith("/addkey") or user_input.lower() == "addkey":
            command_processed = True
            new_key = input("Enter new API key to add: ").strip()
            if new_key:
                if new_key not in api_keys_list:
                    api_keys_list.append(new_key)
                    save_api_keys(api_keys_list)
                    print(f"🔑 New API key ending with ...{new_key[-4:]} added and saved.")
                else:
                    print("ℹ️ This API key is already in the list.")
            else:
                print("ℹ️ No API key entered.")
        
        elif user_input.lower().startswith("/showkeys") or user_input.lower()=="showkeys": # <<< CORRECTED TYPO
            command_processed = True
            if not api_keys_list:
                print("ℹ️ No API keys configured.")
            else:
                print("\n--- Configured API Keys ---")
                for i, key_preview in enumerate(api_keys_list):
                    preview = key_preview[:4] + "..." + key_preview[-4:]
                    print(f"  {i}: {preview} {'(current)' if i == current_api_key_index else ''}")
                print("---------------------------\n")
        
        if command_processed:
            continue # Don't send commands to Gemini

        # Send message to Gemini
        try:
            response = chat.send_message(user_input)
            print(f"🤖 Gemini: {response.text.strip()}\n")
        except Exception as e:
            print(f"❌ Error sending message or receiving response: {e}")
            print("   This could be due to quota exhaustion on the current API key, network issues, or content filtering.")
            if "API_KEY_INVALID" in str(e).upper() or \
               "PERMISSION_DENIED" in str(e).upper() or \
               "QUOTA" in str(e).lower() or \
               "API KEY NOT VALID" in str(e).upper(): # Broader check
                print("   This error often indicates an API key issue (invalid, expired, or quota exceeded).")
                print("   Use '/switchkey' to try another key or '/addkey' to add a new one.")
            # No automatic rotation here, user explicitly uses /switchkey

except KeyboardInterrupt:
    print("\n⏹️ Interrupted. Saving session...")
except Exception as e:
    print(f"\n💥 An unexpected error occurred: {e}")
    print("   Saving current session state before exiting...")
finally:
    # ========= SAVE SHARED CONTEXT ==========
    if 'chat' in locals() and hasattr(chat, 'history'): # Check if chat object exists and has history
        try:
            # Convert chat.history (list of Content objects) to a JSON serializable list of dicts
            serializable_history = []
            for message in chat.history:
                # Rebuild parts structure correctly for JSON
                parts_list = []
                if message.parts: # Check if message.parts is not empty
                    for part in message.parts:
                        if hasattr(part, 'text'):
                            parts_list.append({"text": part.text})
                        # You can add other part types here if your app uses them (e.g., function calls)
                        # elif hasattr(part, 'function_call'):
                        #    parts_list.append({
                        #        "function_call": {
                        #            "name": part.function_call.name,
                        #            "args": dict(part.function_call.args) # Convert args to dict
                        #        }
                        #    })
                
                # Only append if there's a role and some parts (or if role exists and parts is empty for system init)
                if message.role:
                     serializable_history.append({
                        "role": message.role,
                        "parts": parts_list if parts_list else [{"text": ""}] # Ensure 'parts' is always a list, even if empty text
                    })
            
            with open(CONTEXT_FILE, "w", encoding="utf-8") as f:
                json.dump(serializable_history, f, indent=2, ensure_ascii=False)
            print(f"✅ Global conversation context saved to {CONTEXT_FILE}.")

            # NOTE: Google Drive upload logic is removed as per your request to simplify
            #       to the non-Drive version of the script.

        except Exception as e_save:
            print(f"❌ Error saving context: {e_save}")
    else:
        print("ℹ️ No chat history to save.")