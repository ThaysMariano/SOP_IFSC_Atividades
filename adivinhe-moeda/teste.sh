#!/bin/bash
HOST="http://localhost:8080"

echo "=== TESTE: JOGO ADIVINHE A MOEDA (Kauan e Thays)==="

echo -e "\n[1] Reset"
curl -s "$HOST/reset"

echo -e "\n[2] Coletar moeda (bloqueante)"
RESP=$(curl -s "$HOST/collect")
echo "$RESP"

ID=$(echo "$RESP" | grep -oE '"coin_id":[0-9]+' | cut -d: -f2)

echo -e "\n[3] Tentando adivinhar errado"
curl -s "$HOST/guess?id=$ID&value=999"

echo -e "\n[4] Status"
curl -s "$HOST/status"

echo -e "\n[5] Reset de novo"
curl -s "$HOST/reset"

echo -e "\n=== FIM DOS TESTES ==="

