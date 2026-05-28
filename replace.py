import os

def replace_in_files(directory):
    for root, dirs, files in os.walk(directory):
        for file in files:
            if file.endswith('.cpp') or file.endswith('.h'):
                filepath = os.path.join(root, file)
                with open(filepath, 'r', encoding='utf-8') as f:
                    content = f.read()
                
                if '.emplace<TransformComponent>' in content:
                    content = content.replace('.emplace<TransformComponent>', '.get_or_emplace<TransformComponent>')
                    with open(filepath, 'w', encoding='utf-8') as f:
                        f.write(content)
                    print(f"Updated {filepath}")

replace_in_files(r"c:\Users\k024g\source\repos\TD_Engine\Game")
