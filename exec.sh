# Compilazione

    make
    
    read -p "Compilazione effettuata. Premi invio per continuare."
# Esecuzione
# 1) Esecuzione del DS sulla porta 4242
    gnome-terminal -x sh -c "./ds 4242; exec bash"

# 2) Esecuzione di 5 peer sulle porte: 5001, 5002, 5003, 5004, 5005
    for port in {5001..5005}
    do
        gnome-terminal -x sh -c "./peer $port; exec bash"
    done
